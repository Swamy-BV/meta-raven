// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rclcpp/qos.hpp"
#include "rclcpp/time.hpp"
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>

#include "adapters/opencv/camera_motion.h"
#include "node.h"
#include "sensor_msgs/msg/image.hpp"

namespace raven::tracking {
namespace {
constexpr auto kMaximumWidth = 1920U;
constexpr auto kMaximumHeight = 1080U;
constexpr auto kMaximumImageBytes = 16U * 1024U * 1024U;
constexpr int kGrayWidth = 320;
constexpr std::size_t kMaximumFrameLength = 256;
constexpr auto kFutureToleranceNs = 100000000LL;
constexpr int kColorChannels = 3;
constexpr auto kNanoseconds = 1000000000LL;

cv::Mat Grayscale(const sensor_msgs::msg::Image& message) {
  const bool mono = message.encoding == "mono8";
  if (!mono && message.encoding != "rgb8" && message.encoding != "bgr8") {
    throw std::invalid_argument("Camera compensation requires mono8/rgb8/bgr8");
  }
  const auto channels = mono ? 1 : kColorChannels;
  const auto bytes = static_cast<std::size_t>(message.width) * channels;
  if (message.width == 0 || message.height == 0 ||
      message.width > kMaximumWidth || message.height > kMaximumHeight ||
      message.step < bytes || message.data.size() > kMaximumImageBytes ||
      static_cast<std::uint64_t>(message.step) * message.height >
          message.data.size()) {
    throw std::invalid_argument("Invalid compensation image dimensions/stride");
  }
  // OpenCV defines its signed pixel-type constants with bitwise macros.
  // NOLINTBEGIN(hicpp-signed-bitwise)
  cv::Mat pixels(static_cast<int>(message.height),
                 static_cast<int>(message.width), mono ? CV_8UC1 : CV_8UC3);
  // NOLINTEND(hicpp-signed-bitwise)
  for (int row = 0; row < pixels.rows; ++row) {
    std::copy_n(
        message.data.cbegin() + static_cast<std::ptrdiff_t>(row) * message.step,
        bytes, pixels.ptr<unsigned char>(row));
  }
  cv::Mat gray;
  if (mono) {
    gray = std::move(pixels);
  } else {
    cv::cvtColor(
        pixels, gray,
        message.encoding == "rgb8" ? cv::COLOR_RGB2GRAY : cv::COLOR_BGR2GRAY);
  }
  const auto width = std::min(kGrayWidth, gray.cols);
  cv::Mat reduced;
  cv::resize(gray, reduced,
             cv::Size(width, std::max(1, gray.rows * width / gray.cols)), 0, 0,
             cv::INTER_AREA);
  return reduced;
}
}  // namespace

void TrackingNode::ConnectImages() {
  if (!camera_compensation_) {
    return;
  }
  image_input_ = create_subscription<sensor_msgs::msg::Image>(
      image_topic_, rclcpp::SensorDataQoS().keep_last(1),
      [this](const sensor_msgs::msg::Image::ConstSharedPtr& message) {
        ReceiveImage(*message);
      });
}

void TrackingNode::ReceiveImage(const sensor_msgs::msg::Image& message) {
  try {
    const auto stamp = rclcpp::Time(message.header.stamp).nanoseconds();
    const auto age = now().nanoseconds() - stamp;
    if (message.header.frame_id.empty() ||
        message.header.frame_id.size() > kMaximumFrameLength ||
        message.header.frame_id.front() == '/' || stamp <= 0 ||
        age < -kFutureToleranceNs || age > kNanoseconds) {
      throw std::invalid_argument("Invalid compensation image frame/time");
    }
    camera_motion_.Push({stamp, message.header.frame_id,
                         static_cast<int>(message.width),
                         static_cast<int>(message.height), Grayscale(message)});
  } catch (const std::exception& error) {
    camera_motion_.Reset();
    compensation_ = {std::nullopt, error.what(), 0, 0};
  }
}
}  // namespace raven::tracking
