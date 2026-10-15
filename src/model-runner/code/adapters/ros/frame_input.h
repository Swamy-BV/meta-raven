// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/subscription.hpp>

#include "sensor_msgs/msg/image.hpp"
#include <raven/contracts/camera.h>

// One latest ROS sample. Spin runs on the application event loop; Latest runs
// on the inference worker. It returns packed pixels owned by the caller.
class RosFrameInput {
 public:
  explicit RosFrameInput(const std::string& topic);
  void Spin();
  [[nodiscard]] raven::contracts::CameraFrame Latest() const;

 private:
  void Receive(sensor_msgs::msg::Image::ConstSharedPtr image);
  std::shared_ptr<rclcpp::Node> node_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  mutable std::mutex mutex_;
  sensor_msgs::msg::Image::ConstSharedPtr latest_;
  std::uint64_t sequence_ = 0;
};
