// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>

#include "conversion.h"
#include "nav_msgs/msg/odometry.hpp"
#include "px4_msgs/msg/vehicle_odometry.hpp"

namespace raven::odometry {
class OdometryNode final : public rclcpp::Node {
 public:
  OdometryNode();

 private:
  void Receive(const nav_msgs::msg::Odometry& input);
  void PublishHealth();
  Settings settings_;
  std::int64_t last_sample_us_ = 0;
  std::uint8_t reset_counter_ = 0;
  std::uint64_t published_ = 0;
  std::uint64_t rejected_ = 0;
  std::string reason_ = "waiting for calibrated odometry";
  std::chrono::steady_clock::time_point last_received_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr input_;
  rclcpp::Publisher<px4_msgs::msg::VehicleOdometry>::SharedPtr output_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
  rclcpp::TimerBase::SharedPtr timer_;
};
}  // namespace raven::odometry
