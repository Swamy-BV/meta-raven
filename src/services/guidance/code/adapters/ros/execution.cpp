// SPDX-License-Identifier: AGPL-3.0-only
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <rclcpp/time.hpp>

#include "behaviors/trail.h"
#include "core/types.h"
#include "estimation/motion.h"
#include "execution/controller.h"
#include "node.h"
#include "planning/trajectory.h"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "px4_msgs/msg/vehicle_status.hpp"

namespace raven::guidance {
namespace {
constexpr double kMaximumControlGap = 0.25;
constexpr double kNanoseconds = 1000000000.0;
}  // namespace

bool GuidanceNode::HasAction() const { return goal_ || orbit_goal_; }

void GuidanceNode::UpdateAction(const Pose& pose) {
  if (orbit_goal_) {
    UpdateOrbit(pose);
  } else {
    UpdateGoTo(pose);
  }
}

bool GuidanceNode::ArmedOffboard() const {
  return status_ &&
         status_->arming_state ==
             px4_msgs::msg::VehicleStatus::ARMING_STATE_ARMED &&
         status_->nav_state ==
             px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_OFFBOARD &&
         !status_->failsafe;
}

Kinematics GuidanceNode::MeasuredMotion() const {
  if (!position_) {
    throw std::logic_error("Measured vehicle motion unavailable");
  }
  const auto& position = position_.value();
  return {{position.x, position.y, position.z},
          {position.vx, position.vy, position.vz},
          {position.ax, position.ay, position.az}};
}

Kinematics GuidanceNode::InitialReference() const {
  auto initial = MeasuredMotion();
  // Estimated physical acceleration is not the derivative of the commanded
  // reference. On first handover it is unknown, so start reference acceleration
  // at zero. While Guidance owns motion, retain its published derivatives.
  initial.acceleration = {};
  if (streaming_ && smooth_motion_) {
    initial.velocity = smooth_motion_->velocity;
    initial.acceleration = smooth_motion_->acceleration;
  }
  return initial;
}

MotionLimits GuidanceNode::Limits() const {
  return {config_.max_speed, config_.max_acceleration, config_.max_jerk,
          !full_velocity_};
}

void GuidanceNode::ResetExecution() {
  auto measured = InitialReference();
  if (!full_velocity_) {
    measured.velocity[2] = 0;
    measured.acceleration[2] = 0;
  }
  execution_.Reset(measured, Limits());
  smooth_motion_.reset();
}

void GuidanceNode::BeginBraking() {
  if (!streaming_ || braking_) {
    return;
  }
  ResetExecution();
  const auto measured = MeasuredMotion();
  constexpr double kBrakingFactor = 3;
  constexpr double kBrakingMargin = 2;
  const auto speed = std::hypot(measured.velocity[0], measured.velocity[1],
                                measured.velocity[2]);
  braking_deadline_ =
      now().seconds() + kBrakingFactor * speed / config_.max_acceleration +
      kBrakingFactor * config_.max_acceleration / config_.max_jerk +
      kBrakingMargin;
  braking_ = true;
}

bool GuidanceNode::AdvanceControlClock() {
  const auto stamp = now().nanoseconds();
  const auto dt =
      static_cast<double>(stamp - last_control_stamp_ns_) / kNanoseconds;
  last_control_stamp_ns_ = stamp;
  control_step_ = dt;
  return dt >= 0 && dt <= kMaximumControlGap;
}

void GuidanceNode::StopOutput(const std::string& reason) {
  streaming_ = false;
  enabled_ = false;
  braking_ = false;
  FinishGoTo(false, reason);
  FinishOrbit(false, reason);
  motion_.Reset();
  trail_.Reset();
  selected_id_.clear();
  target_.reset();
  state_ = "unavailable";
  reason_ = reason;
}
}  // namespace raven::guidance
