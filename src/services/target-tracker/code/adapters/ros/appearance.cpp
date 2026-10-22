// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "std_msgs/msg/header.hpp"
#include <rclcpp/time.hpp>

#include "adapters/neutron/vehicle_encoder.h"
#include "core/tracker.h"
#include "node.h"
#include "sensor_msgs/msg/image.hpp"

namespace raven::tracking {
namespace {
constexpr std::size_t kFrameCache = 16;
constexpr std::size_t kMaximumWidth = 1280;
constexpr std::size_t kMaximumHeight = 720;
constexpr std::size_t kMaximumBytes = kMaximumWidth * kMaximumHeight * 3;
constexpr std::size_t kCropsPerFrame = 4;
constexpr std::int64_t kCacheNanoseconds = 1000000000;
constexpr double kMinimumScore = 0.5;
constexpr int kChannels = 3;
}  // namespace

void TrackingNode::Frame(
    const sensor_msgs::msg::Image::ConstSharedPtr& message) {
  if (message->encoding != "rgb8" || message->width == 0 ||
      message->height == 0 || message->width > kMaximumWidth ||
      message->height > kMaximumHeight ||
      message->step < message->width * kChannels ||
      message->data.size() > kMaximumBytes ||
      static_cast<std::size_t>(message->step) * message->height !=
          message->data.size() ||
      message->header.frame_id.empty() || message->header.stamp.sec < 0 ||
      message->header.stamp.nanosec >= kCacheNanoseconds) {
    return;
  }
  const auto stamp = rclcpp::Time(message->header.stamp).nanoseconds();
  const auto age = now().nanoseconds() - stamp;
  if (age < 0 || age > kCacheNanoseconds ||
      (!frames_.empty() &&
       stamp <= rclcpp::Time(frames_.back()->header.stamp).nanoseconds())) {
    return;
  }
  frames_.push_back(message);
  while (frames_.size() > kFrameCache ||
         stamp - rclcpp::Time(frames_.front()->header.stamp).nanoseconds() >
             kCacheNanoseconds) {
    frames_.pop_front();
  }
}

void TrackingNode::Encode(std::vector<Observation>& observations,
                          const std_msgs::msg::Header& header) {
  if (!encoder_) {
    return;
  }
  for (auto& observation : observations) {
    observation.appearance_subject =
        std::find(vehicle_classes_.begin(), vehicle_classes_.end(),
                  observation.class_id) != vehicle_classes_.end();
  }
  const auto found = std::find_if(
      frames_.begin(), frames_.end(),
      [&header](const auto& frame) { return frame->header == header; });
  if (found == frames_.end()) {
    ++missing_frames_;
    return;
  }
  const auto& image = **found;
  const RgbFrame frame{
      image.data.data(), image.data.size(), static_cast<int>(image.width),
      static_cast<int>(image.height), static_cast<int>(image.step)};
  std::vector<std::size_t> order(observations.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(),
                   [&observations](auto left, auto right) {
                     const auto& a = observations[left].box;
                     const auto& b = observations[right].box;
                     return a.width * a.height > b.width * b.height;
                   });
  std::size_t crops = 0;
  reid_milliseconds_ = 0;
  for (const auto index : order) {
    auto& observation = observations[index];
    if (observation.score < kMinimumScore ||
        std::find(vehicle_classes_.begin(), vehicle_classes_.end(),
                  observation.class_id) == vehicle_classes_.end()) {
      continue;
    }
    if (crops == kCropsPerFrame) {
      break;
    }
    observation.appearance = encoder_->Encode(frame, observation.box);
    if (!observation.appearance.empty()) {
      ++crops;
      ++encoded_crops_;
      reid_milliseconds_ += encoder_->LastMilliseconds();
    }
  }
}
}  // namespace raven::tracking
