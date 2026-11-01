// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <camera_info_manager/camera_info_manager.hpp>
#include <rclcpp/time.hpp>

#include "application/session.h"
#include "core/types.h"
#include "node.h"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace raven::media {
namespace {
constexpr auto kIntrinsicColumns = 3U;
constexpr auto kNanosecondsPerSecond = 1000000000U;
constexpr auto kProjectionColumns = 4U;

bool ValidCalibration(const sensor_msgs::msg::CameraInfo& info) {
  const auto finite = [](double value) { return std::isfinite(value); };
  return info.width > 0 && info.height > 0 && info.k[0] > 0 &&
         info.k[kIntrinsicColumns + 1] > 0 &&
         std::all_of(info.k.begin(), info.k.end(), finite) &&
         std::all_of(info.p.begin(), info.p.end(), finite) &&
         std::all_of(info.r.begin(), info.r.end(), finite) &&
         std::all_of(info.d.begin(), info.d.end(), finite) &&
         info.binning_x <= 1 && info.binning_y <= 1 && info.roi.x_offset == 0 &&
         info.roi.y_offset == 0 && info.roi.width == 0 && info.roi.height == 0;
}

bool MatchesImageIdentity(const sensor_msgs::msg::CameraInfo& info,
                          const sensor_msgs::msg::Image& image) {
  return info.header.stamp.sec >= 0 &&
         info.header.stamp.nanosec < kNanosecondsPerSecond &&
         info.header.frame_id == image.header.frame_id &&
         rclcpp::Time(info.header.stamp).nanoseconds() <=
             rclcpp::Time(image.header.stamp).nanoseconds();
}

sensor_msgs::msg::CameraInfo ResizeCalibration(
    sensor_msgs::msg::CameraInfo info, const sensor_msgs::msg::Image& image) {
  // The pipeline stretches the complete input image. Distortion coefficients
  // and rectification rotation remain unchanged; pixel-coordinate rows scale.
  const double sx = static_cast<double>(image.width) / info.width;
  const double sy = static_cast<double>(image.height) / info.height;
  for (std::size_t column = 0; column < kIntrinsicColumns; ++column) {
    info.k[column] *= sx;
    info.k[column + kIntrinsicColumns] *= sy;
  }
  for (std::size_t column = 0; column < kProjectionColumns; ++column) {
    info.p[column] *= sx;
    info.p[column + kProjectionColumns] *= sy;
  }
  info.header = image.header;
  info.width = image.width;
  info.height = image.height;
  info.binning_x = 0;
  info.binning_y = 0;
  return info;
}
}  // namespace

sensor_msgs::msg::CameraInfo MediaCaptureNode::CalibrationFor(
    const sensor_msgs::msg::Image& image) const {
  sensor_msgs::msg::CameraInfo unknown;
  unknown.header = image.header;
  unknown.width = image.width;
  unknown.height = image.height;
  const auto& source = session_->Configuration().source;
  auto calibration = calibration_;
  auto source_width = input_width_;
  auto source_height = input_height_;
  const bool missing_input_info =
      calibration.width == 0 && calibration.height == 0;
  if (source.kind != "ros" ||
      (missing_input_info && configured_ros_calibration_)) {
    if (!configured_calibration_valid_ || !camera_info_->isCalibrated()) {
      return unknown;
    }
    calibration = camera_info_->getCameraInfo();
    // An explicit calibration URL may describe a ROS source whose driver
    // publishes CameraInfo only at startup. Never reuse it after source
    // changes.
    if (source.kind != "ros") {
      source_width = static_cast<std::uint32_t>(source.width);
      source_height = static_cast<std::uint32_t>(source.height);
    }
  } else if (!MatchesImageIdentity(calibration, image)) {
    return unknown;
  }
  const bool same_resolution =
      calibration.width == image.width && calibration.height == image.height;
  const bool known_source =
      calibration.width == source_width && calibration.height == source_height;
  if (!ValidCalibration(calibration) || (!same_resolution && !known_source)) {
    return unknown;
  }
  return ResizeCalibration(std::move(calibration), image);
}
}  // namespace raven::media
