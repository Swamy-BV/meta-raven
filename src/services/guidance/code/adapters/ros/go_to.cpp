// SPDX-License-Identifier: AGPL-3.0-only
#include "behaviors/go_to.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include <rclcpp/duration.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/time.hpp>
#include <rclcpp_action/create_server.hpp>
#include <rclcpp_action/server.hpp>
#include <rclcpp_action/server_goal_handle.hpp>
#include <rclcpp_action/types.hpp>

#include "behaviors/follow.h"
#include "behaviors/trail.h"
#include "estimation/motion.h"
#include "execution/controller.h"
#include "node.h"
#include "px4_msgs/msg/offboard_control_mode.hpp"
#include "px4_msgs/msg/trajectory_setpoint.hpp"
#include "raven_interfaces/action/go_to.hpp"
#include "validation/motion.h"

namespace raven::guidance {
namespace {
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kGoalAge = 2;
constexpr auto kNanoseconds = 1000000000U;
constexpr auto kNanosPerMicro = 1000;

Destination ConvertGoal(const raven_interfaces::action::GoTo::Goal& goal) {
  const auto& point = goal.target.point;
  return {{point.y, point.x, -point.z},
          kHalfPi - goal.yaw,
          goal.position_tolerance,
          goal.timeout,
          goal.max_speed};
}
}  // namespace

void GuidanceNode::ConnectGoTo() {
  go_to_ = rclcpp_action::create_server<GoTo>(
      this, "~/go_to",
      [this](const auto& uuid, const auto& goal) {
        return HandleGoToGoal(uuid, goal);
      },
      [this](const auto& goal) {
        return goal == goal_ ? rclcpp_action::CancelResponse::ACCEPT
                             : rclcpp_action::CancelResponse::REJECT;
      },
      [this](const auto& goal) { StartGoTo(goal); });
}

std::string GuidanceNode::PrepareGoTo(const GoTo::Goal& goal) {
  if (enabled_ || braking_ || goal_reserved_ || !FreshTelemetry() ||
      !ArmedOffboard()) {
    return "Motion is busy or fresh armed offboard telemetry is unavailable";
  }
  if (!allow_unmapped_motion_ || !goal.allow_unmapped_motion) {
    return "Obstacle-aware planning unavailable; explicit unmapped opt-in "
           "required";
  }
  if (goal.target.header.stamp.sec < 0 ||
      goal.target.header.stamp.nanosec >= kNanoseconds) {
    return "Invalid goal timestamp";
  }
  const auto age = (now() - rclcpp::Time(goal.target.header.stamp)).seconds();
  if (goal.target.header.frame_id != local_frame_ || age < 0 ||
      age > kGoalAge) {
    return "Goal requires the configured local ENU frame and a fresh timestamp";
  }
  const auto destination = ConvertGoal(goal);
  if (!ValidDestination(destination, config_)) {
    return "Invalid destination, tolerance, timeout or speed";
  }
  const auto initial = InitialReference();
  const MotionLimits limits{destination.speed, config_.max_acceleration,
                            config_.max_jerk};
  if (!trajectory_.Plan(initial, destination.position, limits)) {
    return "Trajectory generation rejected initial reference: velocity=" +
           std::to_string(initial.velocity[0]) + "," +
           std::to_string(initial.velocity[1]) + "," +
           std::to_string(initial.velocity[2]) +
           "; acceleration=" + std::to_string(initial.acceleration[0]) + "," +
           std::to_string(initial.acceleration[1]) + "," +
           std::to_string(initial.acceleration[2]);
  }
  if (trajectory_.Duration() > destination.timeout) {
    return "Trajectory duration exceeds the goal deadline";
  }
  if (!trajectory_.Inside(initial.position, max_displacement_,
                          config_.ground_z - config_.maximum_height,
                          config_.ground_z - config_.minimum_height)) {
    return "Trajectory leaves the configured flight envelope";
  }
  destination_ = destination;
  return {};
}

rclcpp_action::GoalResponse GuidanceNode::HandleGoToGoal(
    const rclcpp_action::GoalUUID& /*uuid*/,
    const std::shared_ptr<const GoTo::Goal>& goal) {
  const auto rejection = PrepareGoTo(*goal);
  if (!rejection.empty()) {
    last_rejection_ = rejection;
    return rclcpp_action::GoalResponse::REJECT;
  }
  // Reserve ownership before the accepted callback; a second queued goal cannot
  // overwrite the prepared trajectory.
  goal_reserved_ = true;
  return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
}

void GuidanceNode::StartGoTo(const std::shared_ptr<GoalHandle>& goal) {
  goal_ = goal;
  selected_id_.clear();
  target_.reset();
  motion_.Reset();
  trail_.Reset();
  enabled_ = false;
  braking_ = false;
  action_stopping_ = false;
  full_velocity_ = true;
  streaming_ = true;
  origin_ = LatestPose().position;
  hold_position_ = origin_;
  command_.yaw = Heading(LatestPose().orientation);
  goal_started_ = now().seconds();
  last_control_stamp_ns_ = now().nanoseconds();
  state_ = "going_to";
  reason_ = "Executing GoTo without obstacle avoidance";
}

void GuidanceNode::UpdateGoTo(const Pose& pose) {
  if (!goal_) {
    return;
  }
  if (goal_->is_canceling()) {
    Hold("GoTo cancelled; braking before releasing the action");
    return;
  }
  const auto elapsed = now().seconds() - goal_started_;
  if (elapsed < 0 || elapsed > destination_.timeout) {
    Hold("GoTo deadline expired");
    return;
  }
  smooth_motion_ = trajectory_.Sample(elapsed);
  if (!ValidMotion(*smooth_motion_, Limits())) {
    StopOutput("GoTo trajectory validation failed");
    return;
  }
  command_.yaw = LimitYaw(
      {command_.yaw, destination_.yaw, config_.max_yaw_rate, control_step_});
  auto feedback = std::make_shared<GoTo::Feedback>();
  feedback->distance_remaining =
      DistanceTo(MeasuredMotion().position, destination_);
  feedback->state = state_;
  goal_->publish_feedback(feedback);
  const auto yaw_error = destination_.yaw - Heading(pose.orientation);
  constexpr double kYawTolerance = 0.1;
  if (elapsed >= trajectory_.Duration() &&
      Arrived(MeasuredMotion(), destination_) &&
      std::abs(std::atan2(std::sin(yaw_error), std::cos(yaw_error))) <=
          kYawTolerance) {
    FinishGoTo(true, "GoTo arrival observed");
    hold_position_ = LatestPose().position;
    command_.velocity = {};
    braking_ = false;
    full_velocity_ = false;
    state_ = "holding";
  }
}

void GuidanceNode::FinishGoTo(const bool success, const std::string& message) {
  if (!goal_) {
    return;
  }
  auto result = std::make_shared<GoTo::Result>();
  result->success = success && !goal_->is_canceling();
  result->message = message;
  if (goal_->is_canceling()) {
    goal_->canceled(result);
  } else if (success) {
    goal_->succeed(result);
  } else {
    goal_->abort(result);
  }
  goal_.reset();
  goal_reserved_ = false;
  action_stopping_ = false;
  reason_ = message;
}

void GuidanceNode::PublishTrajectory() {
  if (!smooth_motion_) {
    StopOutput("Action trajectory sample unavailable");
    return;
  }
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  px4_msgs::msg::OffboardControlMode mode;
  mode.timestamp =
      static_cast<std::uint64_t>(now().nanoseconds() / kNanosPerMicro);
  mode.position = true;
  px4_msgs::msg::TrajectorySetpoint setpoint;
  setpoint.timestamp = mode.timestamp;
  for (std::size_t axis = 0; axis < setpoint.position.size(); ++axis) {
    setpoint.position[axis] =
        static_cast<float>(smooth_motion_->position[axis]);
    setpoint.velocity[axis] =
        static_cast<float>(smooth_motion_->velocity[axis]);
    setpoint.acceleration[axis] =
        static_cast<float>(smooth_motion_->acceleration[axis]);
  }
  setpoint.jerk = {nan, nan, nan};
  setpoint.yaw = static_cast<float>(command_.yaw);
  setpoint.yawspeed = orbit_goal_ ? static_cast<float>(command_.yaw_rate) : nan;
  mode_->publish(mode);
  setpoint_->publish(setpoint);
  ++setpoints_sent_;
}
}  // namespace raven::guidance
