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
#include "estimation/pose.h"
#include "node.h"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "util.h"

namespace raven::guidance {
namespace {
void AddMetric(diagnostic_msgs::msg::DiagnosticStatus& health,
               const std::string& key, const std::string& value) {
  diagnostic_msgs::msg::KeyValue metric;
  metric.key = key;
  metric.value = value;
  health.values.push_back(metric);
}
}  // namespace

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
  AddMetric(health, "state", state_);
  AddMetric(health, "behavior",
            orbit_goal_ ? "orbit"
            : goal_     ? "go_to"
            : enabled_  ? "follow"
                        : "none");
  AddMetric(health, "obstacle_avoidance", "unavailable");
  AddMetric(health, "unmapped_motion_enabled",
            allow_unmapped_motion_ ? "true" : "false");
  AddMetric(health, "local_frame", local_frame_);
  AddMetric(health, "braking", braking_ ? "true" : "false");
  AddMetric(health, "selected_track", selected_id_);
  AddMetric(health, "target_switches", std::to_string(target_switches_));
  AddMetric(health, "px4_namespace", px4_namespace_);
  AddMetric(health, "setpoints_sent", std::to_string(setpoints_sent_));
  AddMetric(health, "control_step_ms",
            std::to_string(std::chrono::duration<double, std::milli>(
                               std::chrono::duration<double>(control_step_))
                               .count()));
  AddMetric(health, "planning_duration_ms",
            std::to_string(planning_duration_ms_));
  AddMetric(health, "max_planning_duration_ms",
            std::to_string(max_planning_duration_ms_));
  AddTargetHealth(health);
  AddMetric(health, "velocity_north", std::to_string(command_.velocity[0]));
  AddMetric(health, "velocity_east", std::to_string(command_.velocity[1]));
  AddMetric(health, "yaw_reference_rad", std::to_string(command_.yaw));
  AddMetric(health, "yaw_rate_reference_rad_s",
            std::to_string(command_.yaw_rate));
  AddMetric(health, "projection",
            orbit_goal_
                ? "box-centre to supplied centre-height plane; rectified pixels"
                : "box-footpoint to configured level ground; rectified pixels");
  report.status.push_back(health);
  health_->publish(report);
}

void GuidanceNode::AddTargetHealth(
    diagnostic_msgs::msg::DiagnosticStatus& health) const {
  AddMetric(health, "observations_used", std::to_string(observations_used_));
  AddMetric(health, "rejected_observations",
            std::to_string(rejected_observations_));
  AddMetric(health, "last_rejection", last_rejection_);
  AddMetric(health, "pose_alignment_failures",
            std::to_string(pose_alignment_failures_));
  AddMetric(health, "source_stamp_ns", std::to_string(target_stamp_ns_));
  AddMetric(health, "target_prediction_horizon_s",
            std::to_string(prediction_limits_.horizon_seconds));
  AddMetric(health, "target_prediction_limit_m",
            std::to_string(prediction_limits_.position_stddev));
  if (target_stamp_ns_ > 0) {
    AddMetric(health, "source_age_ms",
              std::to_string(
                  static_cast<double>(now().nanoseconds() - target_stamp_ns_) /
                  kNanosPerMillisecond));
  }
  if (target_ && position_) {
    const auto range = std::hypot(
        target_.value()[0] - static_cast<double>(position_.value().x),
        target_.value()[1] - static_cast<double>(position_.value().y));
    AddMetric(health, "target_range_m", std::to_string(range));
  }
  const auto prediction =
      enabled_ && state_ == "following" ? FollowingMotion() : std::nullopt;
  const auto motion =
      prediction ? std::optional<Motion>{prediction->motion}
                 : motion_.Estimate(now().seconds(), config_.max_speed);
  if (motion) {
    AddMetric(health, "target_velocity_north",
              std::to_string(motion->velocity[0]));
    AddMetric(health, "target_velocity_east",
              std::to_string(motion->velocity[1]));
    AddMetric(
        health, "target_speed_mps",
        std::to_string(std::hypot(motion->velocity[0], motion->velocity[1])));
  }
  AddMetric(health, "target_control_basis",
            !prediction     ? "unavailable"
            : FreshTarget() ? "fresh_observation"
                            : "bounded_prediction");
  if (prediction) {
    AddMetric(health, "target_prediction_stddev_m",
              std::to_string(prediction->position_stddev));
    if (!FreshTarget()) {
      health.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
    }
  }
}

void GuidanceNode::PublishObservationHealth(
    const std::string& rejection) const {
  diagnostic_msgs::msg::DiagnosticArray report;
  // Each report identifies the image measurement, not inference completion.
  report.header = tracks_.header;
  diagnostic_msgs::msg::DiagnosticStatus health;
  health.name = "autonomy/guidance/observation";
  health.level = rejection.empty()
                     ? diagnostic_msgs::msg::DiagnosticStatus::OK
                     : diagnostic_msgs::msg::DiagnosticStatus::WARN;
  health.message =
      rejection.empty() ? "Target observation accepted" : rejection;
  AddMetric(health, "selected_track", selected_id_);
  AddMetric(health, "pose_alignment",
            "linear_position_shortest_arc_slerp_attitude");
  AddMetric(health, "pose_aligned", alignment_ ? "true" : "false");
  const auto stamp = rclcpp::Time(tracks_.header.stamp).nanoseconds();
  AddMetric(health, "source_age_ms",
            std::to_string(static_cast<double>(now().nanoseconds() - stamp) /
                           kNanosPerMillisecond));
  if (alignment_) {
    AddMetric(health, "position_bracket_ms",
              std::to_string(alignment_->position_span_ms));
    AddMetric(health, "attitude_bracket_ms",
              std::to_string(alignment_->attitude_span_ms));
  }
  const auto& innovation = motion_.LastInnovation();
  if (innovation &&
      innovation->stamp == static_cast<double>(stamp) / kNanoseconds) {
    AddMetric(health, "innovation_north_m",
              std::to_string(innovation->residual[0]));
    AddMetric(health, "innovation_east_m",
              std::to_string(innovation->residual[1]));
    AddMetric(health, "innovation_test_ratio",
              std::to_string(innovation->test_ratio));
  }
  report.status.push_back(health);
  observation_health_->publish(report);
}

}  // namespace raven::guidance
