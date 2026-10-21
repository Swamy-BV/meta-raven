// SPDX-License-Identifier: AGPL-3.0-only
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <ratio>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include <rclcpp/logging.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/time.hpp>

#include "behaviors/follow.h"
#include "core/types.h"
#include "estimation/motion.h"
#include "node.h"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "util.h"

namespace raven::guidance {
// ROS logging expands to generated control statements.
// NOLINTNEXTLINE(readability-function-size)
void GuidanceNode::TraceFrame(std::int64_t source_stamp_ns) const {
  RCLCPP_DEBUG(get_logger(),
               "timing stage=setpoint source_ns=%" PRId64 " ros_ns=%" PRId64
               " steady_ns=%" PRId64,
               source_stamp_ns, now().nanoseconds(),
               static_cast<std::int64_t>(
                   std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count()));
}

void GuidanceNode::PublishHealth() {
  diagnostic_msgs::msg::DiagnosticArray report;
  report.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus health;
  health.name = "autonomy/guidance";
  health.level =
      state_ == "holding" || state_ == "waiting" || state_ == "unavailable"
          ? diagnostic_msgs::msg::DiagnosticStatus::WARN
          : diagnostic_msgs::msg::DiagnosticStatus::OK;
  health.message = reason_;
  const auto add = [&health](const std::string& key, const std::string& value) {
    diagnostic_msgs::msg::KeyValue metric;
    metric.key = key;
    metric.value = value;
    health.values.push_back(metric);
  };
  add("state", state_);
  add("behavior", goal_ ? "go_to" : enabled_ ? "follow" : "none");
  add("obstacle_avoidance", "unavailable");
  add("unmapped_motion_enabled", allow_unmapped_motion_ ? "true" : "false");
  add("local_frame", local_frame_);
  add("braking", braking_ ? "true" : "false");
  add("selected_track", selected_id_);
  add("px4_namespace", px4_namespace_);
  add("setpoints_sent", std::to_string(setpoints_sent_));
  add("control_step_ms",
      std::to_string(std::chrono::duration<double, std::milli>(
                         std::chrono::duration<double>(control_step_))
                         .count()));
  add("planning_duration_ms", std::to_string(planning_duration_ms_));
  add("max_planning_duration_ms", std::to_string(max_planning_duration_ms_));
  add("observations_used", std::to_string(observations_used_));
  add("rejected_observations", std::to_string(rejected_observations_));
  add("last_rejection", last_rejection_);
  add("source_stamp_ns", std::to_string(target_stamp_ns_));
  if (target_stamp_ns_ > 0) {
    add("source_age_ms",
        std::to_string(
            static_cast<double>(now().nanoseconds() - target_stamp_ns_) /
            kNanosPerMillisecond));
  }
  if (target_ && position_) {
    const auto range = std::hypot(
        target_.value()[0] - static_cast<double>(position_.value().x),
        target_.value()[1] - static_cast<double>(position_.value().y));
    add("target_range_m", std::to_string(range));
  }
  const auto motion =
      motion_.Estimate(static_cast<double>(now().nanoseconds()) / kNanoseconds,
                       config_.max_speed);
  if (motion) {
    add("target_velocity_north", std::to_string(motion->velocity[0]));
    add("target_velocity_east", std::to_string(motion->velocity[1]));
    add("target_speed_mps",
        std::to_string(std::hypot(motion->velocity[0], motion->velocity[1])));
  }
  add("velocity_north", std::to_string(command_.velocity[0]));
  add("velocity_east", std::to_string(command_.velocity[1]));
  add("projection",
      "box-footpoint to configured level ground; rectified pixels");
  report.status.push_back(health);
  health_->publish(report);
}

}  // namespace raven::guidance
