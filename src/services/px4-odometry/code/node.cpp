// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include <rclcpp/parameter.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "conversion.h"
#include "nav_msgs/msg/odometry.hpp"
#include "px4_msgs/msg/vehicle_odometry.hpp"

namespace raven::odometry {
namespace {
constexpr double kMaximumSpeed = 50.0;
constexpr auto kResetGapUs = 500000;
constexpr auto kMinimumPublicationUs = 20000;
constexpr auto kHealthPeriod = std::chrono::seconds(1);
constexpr auto kStaleInterval = std::chrono::milliseconds(250);
}  // namespace

OdometryNode::OdometryNode() : Node("px4_odometry") {
  settings_.world_frame =
      declare_parameter<std::string>("world_frame", "global");
  settings_.body_frame = declare_parameter<std::string>("body_frame", "imu");
  settings_.max_speed =
      declare_parameter<double>("max_speed_mps", kDefaultMaxSpeed);
  if (settings_.world_frame.empty() || settings_.body_frame.empty() ||
      settings_.max_speed <= 0 || settings_.max_speed > kMaximumSpeed ||
      !std::isfinite(settings_.max_speed)) {
    throw std::invalid_argument(
        "invalid odometry frame or speed configuration");
  }
  const auto source = declare_parameter<std::string>(
      "input_topic", "/localization/vio/odometry");
  const auto destination = declare_parameter<std::string>(
      "output_topic", "/fmu/in/vehicle_visual_odometry");
  output_ = create_publisher<px4_msgs::msg::VehicleOdometry>(
      destination, rclcpp::SensorDataQoS().keep_last(1));
  input_ = create_subscription<nav_msgs::msg::Odometry>(
      source, rclcpp::SensorDataQoS().keep_last(1),
      [this](const nav_msgs::msg::Odometry::ConstSharedPtr& message) {
        Receive(*message);
      });
  health_ =
      create_publisher<diagnostic_msgs::msg::DiagnosticArray>("health", 1);
  timer_ = create_wall_timer(kHealthPeriod, [this] { PublishHealth(); });
}

void OdometryNode::Receive(const nav_msgs::msg::Odometry& input) {
  // PX4 DDS translates system-clock timestamps into the M7 clock domain.
  // A replay /clock must never become an apparent hardware measurement time.
  if (get_parameter("use_sim_time").as_bool()) {
    reason_ = "hardware odometry requires the ROS system clock";
    ++rejected_;
    return;
  }
  auto result = Convert(input, settings_, now().nanoseconds());
  if (!result.measurement) {
    reason_ = result.error;
    ++rejected_;
    return;
  }
  auto& measurement = *result.measurement;
  const auto sample = static_cast<std::int64_t>(measurement.timestamp_sample);
  if (sample <= last_sample_us_) {
    reason_ =
        "nonincreasing acquisition timestamp; restart after a clock reset";
    ++rejected_;
    return;
  }
  const auto gap = sample - last_sample_us_;
  if (last_sample_us_ != 0 && gap < kMinimumPublicationUs) {
    return;
  }
  if (last_sample_us_ != 0 && gap > kResetGapUs) {
    ++reset_counter_;
  }
  measurement.reset_counter = reset_counter_;
  output_->publish(measurement);
  last_sample_us_ = sample;
  last_received_ = std::chrono::steady_clock::now();
  reason_.clear();
  ++published_;
}

void OdometryNode::PublishHealth() {
  diagnostic_msgs::msg::DiagnosticArray message;
  message.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus status;
  const bool stale =
      published_ == 0 ||
      std::chrono::steady_clock::now() - last_received_ > kStaleInterval;
  status.name = get_fully_qualified_name();
  status.level = stale || !reason_.empty()
                     ? diagnostic_msgs::msg::DiagnosticStatus::WARN
                     : diagnostic_msgs::msg::DiagnosticStatus::OK;
  status.message =
      !reason_.empty()
          ? reason_
          : (stale ? "odometry stopped; no stale measurements resent"
                   : "forwarding fresh odometry");
  diagnostic_msgs::msg::KeyValue published;
  published.key = "published";
  published.value = std::to_string(published_);
  diagnostic_msgs::msg::KeyValue rejected;
  rejected.key = "rejected";
  rejected.value = std::to_string(rejected_);
  status.values = {published, rejected};
  message.status.push_back(status);
  health_->publish(message);
}
}  // namespace raven::odometry
