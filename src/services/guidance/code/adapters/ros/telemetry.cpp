// SPDX-License-Identifier: AGPL-3.0-only
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <rclcpp/time.hpp>

#include "behaviors/follow.h"
#include "core/types.h"
#include "node.h"
#include "px4_msgs/msg/vehicle_attitude.hpp"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "util.h"
#include "vision_msgs/msg/detection2_d_array.hpp"

namespace raven::guidance {
void GuidanceNode::ReceivePosition(
    const px4_msgs::msg::VehicleLocalPosition& message) {
  if (!message.xy_valid || !message.z_valid || !message.v_xy_valid ||
      !message.v_z_valid || !Finite(message.vx) || !Finite(message.vy) ||
      !Finite(message.vz) || !Finite(message.ax) || !Finite(message.ay) ||
      !Finite(message.az) || !Finite(static_cast<double>(message.x)) ||
      !Finite(static_cast<double>(message.y)) ||
      !Finite(static_cast<double>(message.z))) {
    position_.reset();
    return;
  }
  if (position_ && message.timestamp_sample <= position_->timestamp_sample) {
    return;
  }
  if (position_ &&
      (position_.value().xy_reset_counter != message.xy_reset_counter ||
       position_.value().z_reset_counter != message.z_reset_counter ||
       position_.value().heading_reset_counter !=
           message.heading_reset_counter)) {
    Hold("Local position origin reset; output stopped for PX4 failsafe");
    StopOutput("Local position origin reset");
    positions_.clear();
    attitudes_.clear();
    tracks_ = vision_msgs::msg::Detection2DArray{};
    selected_id_.clear();
  }
  if (!positions_.empty() &&
      message.timestamp_sample <= positions_.back().first) {
    return;
  }
  position_ = message;
  Append(positions_, message.timestamp_sample,
         Vector{message.x, message.y, message.z});
  last_position_ = Clock::now();
}

void GuidanceNode::ReceiveAttitude(
    const px4_msgs::msg::VehicleAttitude& message) {
  const Quaternion orientation{message.q[0], message.q[1], message.q[2],
                               message.q[3]};
  if (!ValidPose({{0, 0, 0}, orientation})) {
    return;
  }
  if (!attitudes_.empty() &&
      message.timestamp_sample <= attitudes_.back().first) {
    return;
  }
  Append(attitudes_, message.timestamp_sample, orientation);
  last_attitude_ = Clock::now();
}

Pose GuidanceNode::LatestPose() const {
  if (!position_ || attitudes_.empty()) {
    throw std::logic_error("Vehicle pose unavailable");
  }
  return {{position_.value().x, position_.value().y, position_.value().z},
          attitudes_.back().second};
}

bool GuidanceNode::FreshTelemetry() const {
  const auto current = Clock::now();
  const auto fresh = [current](const Clock::time_point& received) {
    return std::chrono::duration<double>(current - received).count() <=
           kTelemetryTimeout;
  };
  constexpr double kMicrosPerSecond = 1000000.0;
  const auto sample_fresh = [this](const std::uint64_t timestamp) {
    const auto age =
        now().seconds() - static_cast<double>(timestamp) / kMicrosPerSecond;
    return age >= -kFutureTolerance && age <= kTelemetryTimeout;
  };
  return position_ && status_ && !attitudes_.empty() &&
         sample_fresh(position_->timestamp_sample) &&
         sample_fresh(attitudes_.back().first) && fresh(last_position_) &&
         fresh(last_attitude_) &&
         std::chrono::duration<double>(current - last_status_).count() <=
             kStatusTimeout;
}

}  // namespace raven::guidance
