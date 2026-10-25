// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <rclcpp/clock.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/subscription.hpp>

#include "core/frame.h"
#include "sensor_msgs/msg/image.hpp"

// One latest ROS sample. Receive runs on the ROS executor; Latest runs
// on the application thread. The returned view retains its source message
// without copying pixels.
class RosFrameInput {
 public:
  RosFrameInput(rclcpp::Node& node, const std::string& topic);
  [[nodiscard]] raven::vision::Frame Latest() const;

 private:
  void Receive(sensor_msgs::msg::Image::ConstSharedPtr image);
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  mutable std::mutex mutex_;
  sensor_msgs::msg::Image::ConstSharedPtr latest_;
  std::uint64_t sequence_ = 0;
  rclcpp::Clock::SharedPtr clock_;
  std::uint64_t captured_us_ = 0;
  std::uint64_t captured_unix_us_ = 0;
};
