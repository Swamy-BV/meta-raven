// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "estimation/motion.h"
#include "util.h"

namespace raven::guidance {
GuidanceNode::GuidanceNode() : Node("guidance", "/raven/vehicle_1/autonomy") {
  Configure();
  Connect();
  ConnectGoTo();
  ConnectOrbit();
  ConnectTargetOutputs();
}

void GuidanceNode::Connect() {
  const auto sensor = rclcpp::SensorDataQoS().keep_last(kQueueDepth);
  mode_ = create_publisher<px4_msgs::msg::OffboardControlMode>(
      px4_namespace_ + "/fmu/in/offboard_control_mode", sensor);
  setpoint_ = create_publisher<px4_msgs::msg::TrajectorySetpoint>(
      px4_namespace_ + "/fmu/in/trajectory_setpoint", sensor);
  health_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "~/health", rclcpp::QoS(1).reliable().transient_local());
  input_ = create_subscription<vision_msgs::msg::Detection2DArray>(
      tracks_topic_, kQueueDepth,
      [this](const vision_msgs::msg::Detection2DArray& message) {
        ReceiveTracks(message);
      });
  calibration_ = create_subscription<sensor_msgs::msg::CameraInfo>(
      info_topic_, sensor, [this](const sensor_msgs::msg::CameraInfo& message) {
        ReceiveInfo(message);
      });
  metadata_ = create_subscription<vision_msgs::msg::VisionInfo>(
      metadata_topic_, rclcpp::QoS(1).reliable().transient_local(),
      [this](const vision_msgs::msg::VisionInfo& message) {
        ReceiveMetadata(message);
      });
  labels_ = create_subscription<vision_msgs::msg::LabelInfo>(
      labels_topic_, rclcpp::QoS(1).reliable().transient_local(),
      [this](const vision_msgs::msg::LabelInfo& message) {
        ReceiveLabels(message);
      });
  position_input_ = create_subscription<px4_msgs::msg::VehicleLocalPosition>(
      px4_namespace_ + "/fmu/out/vehicle_local_position_v1", sensor,
      [this](const px4_msgs::msg::VehicleLocalPosition& message) {
        ReceivePosition(message);
      });
  attitude_input_ = create_subscription<px4_msgs::msg::VehicleAttitude>(
      px4_namespace_ + "/fmu/out/vehicle_attitude", sensor,
      [this](const px4_msgs::msg::VehicleAttitude& message) {
        ReceiveAttitude(message);
      });
  status_input_ = create_subscription<px4_msgs::msg::VehicleStatus>(
      px4_namespace_ + "/fmu/out/vehicle_status_v4", sensor,
      [this](const px4_msgs::msg::VehicleStatus& message) {
        status_ = message;
        last_status_ = Clock::now();
      });
  select_ = create_service<Select>(
      "~/select_target", [this](const Select::Request::SharedPtr& request,
                                const Select::Response::SharedPtr& response) {
        SelectTarget(*request, *response);
      });
  enable_ = create_service<Enable>(
      "~/set_following", [this](const Enable::Request::SharedPtr& request,
                                const Enable::Response::SharedPtr& response) {
        EnableFollowing(*request, *response);
      });
  control_timer_ = create_wall_timer(kPeriod, [this] { Tick(); });
  health_timer_ =
      create_wall_timer(std::chrono::seconds(1), [this] { PublishHealth(); });
}

void GuidanceNode::Hold(const std::string& reason) {
  if (streaming_ && !FreshTelemetry()) {
    StopOutput("Cannot brake with stale telemetry: " + reason);
    return;
  }
  if (HasAction() && !action_stopping_) {
    full_velocity_ = true;
    action_stopping_ = true;
  }
  if (enabled_ && position_) {
    hold_position_ = {position_.value().x, position_.value().y,
                      position_.value().z};
  }
  enabled_ = false;
  BeginBraking();
  target_.reset();
  motion_.Reset();
  trail_.Reset();
  state_ = streaming_ ? "holding" : "idle";
  reason_ = reason;
}

void GuidanceNode::Tick() {
  if (!streaming_) {
    return;
  }
  if (!FreshTelemetry()) {
    StopOutput("Vehicle telemetry expired");
    return;
  }
  if (!ArmedOffboard()) {
    StopOutput("Vehicle left armed Offboard authority");
    return;
  }
  if (!AdvanceControlClock()) {
    StopOutput("Control clock gap exceeded its operating limit");
    return;
  }
  const auto pose = LatestPose();
  if ((enabled_ || (HasAction() && !action_stopping_)) &&
      std::hypot(pose.position[0] - origin_[0], pose.position[1] - origin_[1]) >
          max_displacement_) {
    Hold("Guidance displacement limit reached");
  }
  if (HasAction() && !action_stopping_) {
    UpdateAction(pose);
  } else if (enabled_) {
    UpdateFollowingState();
    UpdateCommand(pose);
  }
  UpdateBraking(pose);
  if (HasAction() && action_stopping_ && !braking_) {
    FinishGoTo(false, reason_);
    FinishOrbit(false, reason_);
  }
  if (streaming_) {
    PublishCommand();
  }
}

void GuidanceNode::UpdateFollowingState() {
  const auto age =
      std::chrono::duration<double>(Clock::now() - last_target_).count();
  if (age > loss_timeout_seconds_) {
    Hold("Selected identity expired; explicit selection and enable required");
  } else if (age > timeout_seconds_ || !FreshTarget()) {
    WaitForTarget();
  } else {
    state_ = "following";
    reason_ = "Following the same selected identity";
  }
}

void GuidanceNode::UpdateBraking(const Pose& pose) {
  if (!braking_ || !streaming_ || control_step_ <= 0) {
    return;
  }
  if (now().seconds() > braking_deadline_) {
    StopOutput("Braking deadline expired; releasing to PX4 failsafe");
    return;
  }
  smooth_motion_ = execution_.Velocity(Vector{}, control_step_);
  if (!smooth_motion_) {
    StopOutput("Braking motion generation failed");
    return;
  }
  command_.velocity = smooth_motion_->velocity;
  const auto measured = MeasuredMotion();
  constexpr double kReferenceTolerance = 0.001;
  if (std::hypot(measured.velocity[0], measured.velocity[1],
                 measured.velocity[2]) > kStoppedSpeed ||
      std::hypot(command_.velocity[0], command_.velocity[1],
                 command_.velocity[2]) > kReferenceTolerance ||
      std::hypot(smooth_motion_->acceleration[0],
                 smooth_motion_->acceleration[1],
                 smooth_motion_->acceleration[2]) > kReferenceTolerance) {
    return;
  }
  hold_position_ = pose.position;
  command_.velocity = {};
  smooth_motion_->velocity = {};
  smooth_motion_->acceleration = {};
  braking_ = false;
  full_velocity_ = false;
}

void GuidanceNode::PublishCommand() {
  if (HasAction() && !action_stopping_) {
    PublishTrajectory();
    return;
  }
  px4_msgs::msg::OffboardControlMode mode;
  mode.timestamp =
      static_cast<std::uint64_t>(now().nanoseconds() / kNanosPerMicro);
  mode.position = !full_velocity_;
  mode.velocity = full_velocity_;
  mode_->publish(mode);
  const auto setpoint = MakeSetpoint(mode.timestamp);
  TraceFrame(enabled_ && state_ == "following" ? target_stamp_ns_ : 0);
  setpoint_->publish(setpoint);
  ++setpoints_sent_;
}

px4_msgs::msg::TrajectorySetpoint GuidanceNode::MakeSetpoint(
    const std::uint64_t timestamp) const {
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  px4_msgs::msg::TrajectorySetpoint setpoint;
  setpoint.timestamp = timestamp;
  setpoint.position = {static_cast<float>(hold_position_[0]),
                       static_cast<float>(hold_position_[1]),
                       static_cast<float>(hold_position_[2])};
  setpoint.velocity = {nan, nan, nan};
  setpoint.acceleration = {nan, nan, nan};
  setpoint.jerk = {nan, nan, nan};
  setpoint.yaw = static_cast<float>(command_.yaw);
  // PX4 feeds this reference rate forward to the attitude controller. A
  // changing yaw angle with zero rate creates a tracking lag at corners.
  setpoint.yawspeed = enabled_ && state_ == "following"
                          ? static_cast<float>(command_.yaw_rate)
                          : 0;
  if (!(enabled_ && state_ == "following") && !braking_) {
    return setpoint;
  }
  setpoint.position[0] = nan;
  setpoint.position[1] = nan;
  constexpr auto kHorizontalAxes = 2U;
  for (std::size_t axis = 0; axis < kHorizontalAxes; ++axis) {
    setpoint.velocity[axis] = static_cast<float>(command_.velocity[axis]);
    if (smooth_motion_) {
      setpoint.acceleration[axis] =
          static_cast<float>(smooth_motion_->acceleration[axis]);
    }
  }
  if (full_velocity_) {
    setpoint.position[2] = nan;
    setpoint.velocity[2] = static_cast<float>(command_.velocity[2]);
    if (smooth_motion_) {
      setpoint.acceleration[2] =
          static_cast<float>(smooth_motion_->acceleration[2]);
    }
  }
  return setpoint;
}
}  // namespace raven::guidance
