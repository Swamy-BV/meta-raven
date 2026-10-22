// SPDX-License-Identifier: AGPL-3.0-only
#include "behaviors/orbit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rclcpp/time.hpp>
#include <rclcpp_action/create_server.hpp>
#include <rclcpp_action/server.hpp>
#include <rclcpp_action/server_goal_handle.hpp>

#include "behaviors/follow.h"
#include "behaviors/go_to.h"
#include "core/types.h"
#include "estimation/motion.h"
#include "execution/controller.h"
#include "node.h"
#include "planning/trajectory.h"
#include "raven_interfaces/action/orbit.hpp"
#include "validation/motion.h"
#include "vision_msgs/msg/detection2_d.hpp"

namespace raven::guidance {
namespace {
constexpr double kMaximumTimeout = 300;
constexpr double kSelectionDwell = 1;
constexpr double kStationarySpeed = 0.5;
constexpr double kArrivalTolerance = 0.3;
constexpr double kHeadingTolerance = 0.1;
constexpr double kMaximumCenterDrift = 3;
constexpr double kMaximumPathError = 1;
constexpr double kMaximumHeightError = 0.75;
constexpr double kRevolution = 6.283185307179586;
constexpr double kAngularTolerance = 0.05;
constexpr double kArrivalMargin = 10;
}  // namespace

void GuidanceNode::ConnectOrbit() {
  orbit_action_ = rclcpp_action::create_server<Orbit>(
      this, "~/orbit",
      [this](const auto& /*uuid*/, const auto& goal) {
        const auto rejection = PrepareOrbit(*goal);
        if (!rejection.empty()) {
          last_rejection_ = rejection;
          return rclcpp_action::GoalResponse::REJECT;
        }
        goal_reserved_ = true;
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [this](const auto& goal) {
        return goal == orbit_goal_ ? rclcpp_action::CancelResponse::ACCEPT
                                   : rclcpp_action::CancelResponse::REJECT;
      },
      [this](const auto& goal) { StartOrbit(goal); });
}

std::string GuidanceNode::PrepareOrbit(const Orbit::Goal& goal) {
  if (enabled_ || braking_ || goal_reserved_ || !FreshTelemetry() ||
      !ArmedOffboard()) {
    return "Motion is busy or fresh armed offboard telemetry is unavailable";
  }
  if (!allow_unmapped_motion_ || !goal.allow_unmapped_motion) {
    return "Obstacle-aware planning unavailable; explicit unmapped opt-in "
           "required";
  }
  if (!std::isfinite(goal.timeout) || goal.timeout <= 0 ||
      goal.timeout > kMaximumTimeout) {
    return "Invalid orbit deadline";
  }
  const auto dwell =
      std::chrono::duration<double>(Clock::now() - selected_since_).count();
  if (state_ != "selected" || selected_id_.empty() || !target_ || !info_ ||
      !FreshTarget() || dwell < kSelectionDwell) {
    return "Select a calibrated visible stationary target for at least one "
           "second";
  }
  const auto motion = motion_.Estimate(now().seconds(), config_.max_speed);
  if (!motion ||
      std::hypot(motion->velocity[0], motion->velocity[1]) > kStationarySpeed) {
    return "First orbit implementation requires a stationary target";
  }
  const auto initial = MeasuredMotion();
  const auto height = config_.ground_z - initial.position[2];
  if (!std::isfinite(goal.target_center_height) ||
      goal.target_center_height < 0 || goal.target_center_height >= height) {
    return "A finite target centre-height prior below the aircraft is required";
  }
  if (height < config_.minimum_height || height > config_.maximum_height ||
      std::hypot(initial.velocity[0], initial.velocity[1],
                 initial.velocity[2]) > kArrivalTolerance) {
    return "Orbit must start from a stable hover inside the height envelope";
  }
  return PlanOrbit(goal);
}

std::string GuidanceNode::PlanOrbit(const Orbit::Goal& goal) {
  const auto found = std::find_if(
      tracks_.detections.begin(), tracks_.detections.end(),
      [this](const auto& detection) { return detection.id == selected_id_; });
  if (found == tracks_.detections.end()) {
    return "Selected target is no longer visible";
  }
  const auto projection = TargetPoint(*found, goal.target_center_height);
  if (!projection.point) {
    return projection.reason;
  }
  const auto initial = InitialReference();
  const OrbitRequest request{*projection.point,    initial.position,
                             goal.radius,          goal.max_speed,
                             config_.max_yaw_rate, goal.clockwise};
  if (goal.radius > config_.max_range || !orbit_plan_.Plan(request, Limits())) {
    return "Invalid radius, speed or orbit motion limits";
  }
  const auto center_distance =
      std::hypot(request.center[0] - initial.position[0],
                 request.center[1] - initial.position[1]);
  if (center_distance + goal.radius > max_displacement_) {
    return "Complete orbit leaves the configured displacement envelope";
  }
  const MotionLimits limits{goal.max_speed, config_.max_acceleration,
                            config_.max_jerk};
  const auto start = orbit_plan_.Sample(0);
  if (!trajectory_.Plan(initial, start.position, limits) ||
      !trajectory_.Inside(initial.position, max_displacement_,
                          config_.ground_z - config_.maximum_height,
                          config_.ground_z - config_.minimum_height)) {
    return "Orbit entry trajectory leaves the configured flight envelope";
  }
  if (trajectory_.Duration() + orbit_plan_.Duration() + kArrivalMargin >
      goal.timeout) {
    return "Orbit and entry duration exceed the requested deadline";
  }
  return {};
}

void GuidanceNode::StartOrbit(const std::shared_ptr<OrbitHandle>& goal) {
  orbit_goal_ = goal;
  enabled_ = false;
  braking_ = false;
  action_stopping_ = false;
  full_velocity_ = true;
  streaming_ = true;
  origin_ = LatestPose().position;
  hold_position_ = origin_;
  command_.yaw = Heading(LatestPose().orientation);
  command_.yaw_rate = 0;
  goal_started_ = now().seconds();
  last_control_stamp_ns_ = now().nanoseconds();
  orbit_entering_ = true;
  orbit_travel_ = 0;
  state_ = "orbit_entry";
  reason_ =
      "Approaching the orbit radius around the selected stationary target";
}

void GuidanceNode::UpdateOrbit(const Pose& pose) {
  if (orbit_goal_->is_canceling()) {
    Hold("Orbit cancelled; braking before releasing the action");
    return;
  }
  const auto elapsed = now().seconds() - goal_started_;
  if (elapsed < 0 || elapsed > orbit_goal_->get_goal()->timeout) {
    Hold("Orbit deadline expired");
    return;
  }
  if (!FreshTarget()) {
    Hold("Orbit target observation expired; explicit selection required");
    return;
  }
  UpdateOrbitHeading(pose);
  UpdateOrbitPath(pose);
}

void GuidanceNode::ObserveOrbit(const Vector& point, const std::int64_t stamp) {
  // A fixed POI does not use the following behavior's constant-velocity filter.
  // It projects the box centre onto the supplied centre-height plane.
  // Source/class/identity, calibration and capture-time pose are checked before
  // this callback. Only spatially consistent observations renew freshness.
  const auto drift = std::hypot(point[0] - orbit_plan_.Center()[0],
                                point[1] - orbit_plan_.Center()[1]);
  if (!std::isfinite(drift) || drift > kMaximumCenterDrift) {
    ++rejected_observations_;
    last_rejection_ = "Selected object centre outside frozen orbit bound";
    PublishObservationHealth(last_rejection_);
    return;
  }
  target_ = point;
  target_stamp_ns_ = stamp;
  last_target_ = Clock::now();
  ++observations_used_;
  last_rejection_.clear();
  PublishPoint(orbit_plan_.Center(), estimate_output_);
  PublishObservationHealth("");
}

void GuidanceNode::UpdateOrbitHeading(const Pose& pose) {
  const auto yaw =
      LimitYaw({command_.yaw, OrbitAzimuth(orbit_plan_.Center(), pose.position),
                config_.max_yaw_rate, control_step_});
  command_.yaw_rate = control_step_ > 0
                          ? AngleDifference(yaw, command_.yaw) / control_step_
                          : 0;
  command_.yaw = yaw;
}

void GuidanceNode::UpdateOrbitPath(const Pose& pose) {
  const auto elapsed =
      now().seconds() - (orbit_entering_ ? goal_started_ : orbit_started_);
  smooth_motion_ = orbit_entering_ ? trajectory_.Sample(elapsed)
                                   : orbit_plan_.Sample(elapsed);
  if (!ValidMotion(*smooth_motion_, Limits())) {
    StopOutput("Orbit trajectory validation failed");
    return;
  }
  const auto heading_error = std::abs(
      AngleDifference(OrbitAzimuth(orbit_plan_.Center(), pose.position),
                      Heading(pose.orientation)));
  const auto endpoint = orbit_plan_.Sample(0).position;
  const Destination arrival{endpoint, command_.yaw, kArrivalTolerance, 0, 0};
  if (orbit_entering_) {
    if (elapsed >= trajectory_.Duration() &&
        Arrived(MeasuredMotion(), arrival) &&
        heading_error <= kHeadingTolerance) {
      orbit_entering_ = false;
      orbit_started_ = now().seconds();
      orbit_azimuth_ = OrbitAzimuth(pose.position, orbit_plan_.Center());
      state_ = "orbiting";
      reason_ = "Orbiting the selected identity around a frozen object centre";
    }
    return;
  }
  const auto azimuth = OrbitAzimuth(pose.position, orbit_plan_.Center());
  orbit_travel_ +=
      orbit_plan_.Direction() * AngleDifference(azimuth, orbit_azimuth_);
  orbit_azimuth_ = azimuth;
  const auto radius_error =
      std::hypot(pose.position[0] - orbit_plan_.Center()[0],
                 pose.position[1] - orbit_plan_.Center()[1]) -
      orbit_plan_.Radius();
  if (std::abs(radius_error) > kMaximumPathError ||
      std::abs(pose.position[2] - endpoint[2]) > kMaximumHeightError) {
    Hold("Measured orbit radius or altitude left the tracking envelope");
    return;
  }
  auto feedback = std::make_shared<Orbit::Feedback>();
  feedback->angle_completed = orbit_travel_;
  feedback->radius_error = radius_error;
  feedback->state = state_;
  orbit_goal_->publish_feedback(feedback);
  if (elapsed >= orbit_plan_.Duration() &&
      orbit_travel_ >= kRevolution - kAngularTolerance &&
      Arrived(MeasuredMotion(), arrival) &&
      heading_error <= kHeadingTolerance) {
    FinishOrbit(true, "Full selected-target orbit and arrival observed");
    hold_position_ = pose.position;
    command_.velocity = {};
    command_.yaw_rate = 0;
    full_velocity_ = false;
    state_ = "holding";
  }
}

void GuidanceNode::FinishOrbit(const bool success, const std::string& message) {
  if (!orbit_goal_) {
    return;
  }
  auto result = std::make_shared<Orbit::Result>();
  result->success = success && !orbit_goal_->is_canceling();
  result->message = message;
  if (orbit_goal_->is_canceling()) {
    orbit_goal_->canceled(result);
  } else if (success) {
    orbit_goal_->succeed(result);
  } else {
    orbit_goal_->abort(result);
  }
  orbit_goal_.reset();
  goal_reserved_ = false;
  action_stopping_ = false;
  reason_ = message;
}
}  // namespace raven::guidance
