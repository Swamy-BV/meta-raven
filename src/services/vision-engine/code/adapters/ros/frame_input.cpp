// SPDX-License-Identifier: AGPL-3.0-only
#include "frame_input.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glib.h>
#include <rclcpp/qos.hpp>

#include "sensor_msgs/msg/image.hpp"
#include <raven/contracts/camera.h>

namespace {
constexpr auto kMinDimension = 64;
constexpr auto kMaxWidth = 1920;
constexpr auto kMaxHeight = 1080;
constexpr auto kChannels = 3;
constexpr std::uint64_t kMicrosPerSecond = 1000000;
constexpr std::uint32_t kNanosPerMicro = 1000;
constexpr std::uint32_t kNanosPerSecond = 1000000000;

bool Valid(const sensor_msgs::msg::Image& image) {
  return image.encoding == "rgb8" && image.width >= kMinDimension &&
         image.width <= kMaxWidth && image.height >= kMinDimension &&
         image.height <= kMaxHeight && image.step >= image.width * kChannels &&
         image.data.size() ==
             static_cast<std::size_t>(image.step) * image.height &&
         image.data.size() <= raven::contracts::kMaxCameraBytes &&
         image.header.stamp.sec > 0 &&
         image.header.stamp.nanosec < kNanosPerSecond &&
         !image.header.frame_id.empty();
}
}  // namespace

RosFrameInput::RosFrameInput(rclcpp::Node& node, const std::string& topic) {
  if (topic.empty()) {
    throw std::invalid_argument("camera topic must not be empty");
  }
  subscription_ = node.create_subscription<sensor_msgs::msg::Image>(
      topic, rclcpp::SensorDataQoS().keep_last(1),
      [this](sensor_msgs::msg::Image::ConstSharedPtr image) {
        Receive(std::move(image));
      });
}

void RosFrameInput::Receive(sensor_msgs::msg::Image::ConstSharedPtr image) {
  std::lock_guard<std::mutex> const lock(mutex_);
  ++sequence_;
  // Invalid input clears the retained sample, so it cannot masquerade as a
  // valid stream. Latest reports the missing/invalid-frame failure to the job.
  latest_ = Valid(*image) ? std::move(image) : nullptr;
}

raven::contracts::CameraFrame RosFrameInput::Latest() const {
  sensor_msgs::msg::Image::ConstSharedPtr image;
  raven::contracts::CameraFrame frame;
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    image = latest_;
    frame.sequence = sequence_;
  }
  if (!image) {
    throw std::runtime_error("no valid ROS RGB camera frame");
  }
  frame.source_stamp_ns =
      static_cast<std::uint64_t>(image->header.stamp.sec) * kNanosPerSecond +
      image->header.stamp.nanosec;
  frame.captured_unix_us =
      static_cast<std::uint64_t>(image->header.stamp.sec) * kMicrosPerSecond +
      image->header.stamp.nanosec / kNanosPerMicro;
  const auto wall = static_cast<std::uint64_t>(g_get_real_time());
  const auto monotonic = static_cast<std::uint64_t>(g_get_monotonic_time());
  if (frame.captured_unix_us > wall ||
      wall - frame.captured_unix_us >= monotonic) {
    throw std::runtime_error("camera timestamp is outside the local clock");
  }
  frame.captured_us = monotonic - (wall - frame.captured_unix_us);
  frame.source_time_mapped = true;
  frame.optical_frame = image->header.frame_id;
  frame.width = static_cast<int>(image->width);
  frame.height = static_cast<int>(image->height);
  const auto row = static_cast<std::size_t>(image->width) * kChannels;
  frame.data.resize(row * image->height);
  for (std::uint32_t y = 0; y < image->height; ++y) {
    std::memcpy(frame.data.data() + y * row,
                image->data.data() + static_cast<std::size_t>(y) * image->step,
                row);
  }
  return frame;
}
