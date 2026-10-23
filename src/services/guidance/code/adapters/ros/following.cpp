// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <ratio>
#include <string>
#include <vector>

#include "rclcpp/time.hpp"

#include "behaviors/follow.h"
#include "behaviors/trail.h"
#include "core/types.h"
#include "estimation/motion.h"
#include "execution/controller.h"
#include "node.h"
#include "planning/trajectory.h"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "px4_msgs/msg/vehicle_status.hpp"
#include "util.h"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/object_hypothesis_with_pose.hpp"

namespace raven::guidance {
void GuidanceNode::SelectTarget(const Select::Request& request,
                                Select::Response& response) {
  response.success = false;
  if (enabled_ || goal_reserved_) {
    response.message = "Stop the active behavior before changing the target";
    return;
  }
  if (request.track_id.empty() || request.track_id.size() > kMaxIdLength ||
      !FreshTracks() || model_key_.empty()) {
    response.message = "A fresh visible track and vision metadata are required";
    return;
  }
  const auto found = std::find_if(
      tracks_.detections.begin(), tracks_.detections.end(),
      [this, &request](const auto& detection) {
        return detection.id == request.track_id && Supported(detection);
      });
  if (found == tracks_.detections.end()) {
    response.message = "Person or car track is not visible";
    return;
  }
  motion_.Reset();
  trail_.Reset();
  target_.reset();
  target_stamp_ns_ = 0;
  selected_since_ = Clock::now();
  selected_id_ = request.track_id;
  selected_frame_ = tracks_.header.frame_id;
  selected_class_ = found->results.front().hypothesis.class_id;
  state_ = "selected";
  reason_ = "Target selected; following is not enabled";
  response.success = true;
  response.message = reason_;
}

void GuidanceNode::EnableFollowing(const Enable::Request& request,
                                   Enable::Response& response) {
  response.success = false;
  if (goal_reserved_) {
    response.message = "An action owns motion; cancel it first";
    return;
  }
  if (request.data && braking_ && !enabled_) {
    response.message = "Wait for braking to finish before activating following";
    return;
  }
  if (request.data && !allow_unmapped_motion_) {
    response.message =
        "Obstacle-aware planning unavailable; unmapped motion is disabled";
    return;
  }
  if (request.data && enabled_) {
    response.success = true;
    response.message = reason_;
    return;
  }
  if (!request.data) {
    Hold("Following stopped by request");
    selected_id_.clear();
    response.success = true;
    response.message = reason_;
    return;
  }
  if (!position_ || !status_ || !FreshTelemetry() || !FreshTracks() ||
      selected_id_.empty() || !info_ ||
      status_.value().arming_state !=
          px4_msgs::msg::VehicleStatus::ARMING_STATE_ARMED ||
      status_.value().nav_state !=
          px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_OFFBOARD ||
      status_.value().failsafe) {
    response.message =
        "Select a target with calibrated imagery and fresh telemetry; vehicle "
        "must already be armed in offboard";
    return;
  }
  const auto height =
      config_.ground_z - static_cast<double>(position_.value().z);
  if (height < config_.minimum_height || height > config_.maximum_height) {
    response.message =
        "Following altitude outside configured operating envelope";
    return;
  }
  const auto found = std::find_if(
      tracks_.detections.begin(), tracks_.detections.end(),
      [this](const auto& detection) { return detection.id == selected_id_; });
  if (found == tracks_.detections.end()) {
    response.message = "Selected target is no longer visible";
    return;
  }
  const auto projection = TargetPoint(*found, std::nullopt);
  target_ = projection.point;
  if (!target_) {
    response.message = projection.reason;
    return;
  }
  response.success = StartFollowing();
  response.message = reason_;
}

bool GuidanceNode::StartFollowing() {
  if (!target_) {
    reason_ = "Selected target estimate unavailable";
    return false;
  }
  origin_ = LatestPose().position;
  hold_position_ = origin_;
  command_ = {
      {0, 0, 0},
      Heading(LatestPose().orientation),
      std::atan2((*target_)[1] - origin_[1], (*target_)[0] - origin_[0])};
  last_target_ = Clock::now();
  target_stamp_ns_ = rclcpp::Time(tracks_.header.stamp).nanoseconds();
  planning_duration_ms_ = 0;
  max_planning_duration_ms_ = 0;
  motion_.Observe(*target_,
                  static_cast<double>(target_stamp_ns_) / kNanoseconds);
  full_velocity_ = false;
  ResetExecution();
  last_control_stamp_ns_ = now().nanoseconds();
  enabled_ = true;
  streaming_ = true;
  state_ = "following";
  reason_ = "Following selected target on a level ground plane";
  return true;
}

std::optional<MotionPrediction> GuidanceNode::FollowingMotion() const {
  return motion_.Predict(now().seconds(), config_.max_speed,
                         prediction_limits_);
}

void GuidanceNode::UpdateCommand(const Pose& pose) {
  const auto dt = control_step_;
  if (!enabled_ || state_ != "following" || !target_ || dt <= 0) {
    return;
  }
  const auto prediction = FollowingMotion();
  if (!prediction) {
    WaitForTarget();
    return;
  }
  ResumeVelocity();
  const auto& motion = prediction->motion;
  const auto speed = std::hypot(motion.velocity[0], motion.velocity[1]);
  // Consume recorded trail during a bounded visual gap. Fresh-frame behavior
  // is unchanged; never append predicted points as camera measurements.
  const auto advance =
      speed * std::max(0.0, prediction->age_seconds - timeout_seconds_);
  command_ = Follow(
      {pose, motion.position, motion.velocity, command_.velocity, command_.yaw,
       dt, command_.bearing,
       config_.follow_path
           ? trail_.Reference(std::max(0.0, config_.distance - advance), speed)
           : std::nullopt},
      config_);
  const auto planning_start = Clock::now();
  smooth_motion_ = execution_.Velocity(command_.velocity, dt);
  planning_duration_ms_ =
      std::chrono::duration<double, std::milli>(Clock::now() - planning_start)
          .count();
  max_planning_duration_ms_ =
      std::max(max_planning_duration_ms_, planning_duration_ms_);
  if (!smooth_motion_) {
    StopOutput("Following motion generation failed");
    return;
  }
  command_.velocity = smooth_motion_->velocity;
}

void GuidanceNode::ResumeVelocity() {
  if (!braking_) {
    return;
  }
  ResetExecution();
  command_.velocity = MeasuredMotion().velocity;
  command_.velocity[2] = 0;
  braking_ = false;
}

void GuidanceNode::WaitForTarget() {
  if (state_ == "waiting") {
    return;
  }
  const auto pose = LatestPose();
  hold_position_[0] = pose.position[0];
  hold_position_[1] = pose.position[1];
  BeginBraking();
  state_ = "waiting";
  reason_ = "Brief detection gap; holding for the same selected identity";
}

}  // namespace raven::guidance
