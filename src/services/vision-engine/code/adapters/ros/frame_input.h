// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <rclcpp/node.hpp>
#include <rclcpp/subscription.hpp>

#include "sensor_msgs/msg/image.hpp"
#include <raven/contracts/camera.h>

// One latest ROS sample. Receive runs on the ROS executor; Latest runs
// on the application thread. It returns packed pixels owned by the caller.
class RosFrameInput {
 public:
  RosFrameInput(rclcpp::Node& node, const std::string& topic);
  [[nodiscard]] raven::contracts::CameraFrame Latest() const;

 private:
  void Receive(sensor_msgs::msg::Image::ConstSharedPtr image);
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  mutable std::mutex mutex_;
  sensor_msgs::msg::Image::ConstSharedPtr latest_;
  std::uint64_t sequence_ = 0;
};
