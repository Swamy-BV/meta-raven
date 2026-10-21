// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "sensor_msgs/msg/camera_info.hpp"

namespace raven::guidance {

inline constexpr double kStoppedSpeed = 0.3;
inline constexpr auto kNanosPerMillisecond = 1000000.0;
inline constexpr auto kPeriod = std::chrono::milliseconds(50);
inline constexpr double kTelemetryTimeout = 0.5;
inline constexpr double kStatusTimeout = 2.0;
inline constexpr double kPoseToleranceUs = 100000;
inline constexpr auto kHistory = 120U;
inline constexpr auto kMaxDetections = 128U;
inline constexpr auto kMaxIdLength = 256U;
inline constexpr auto kNanoseconds = 1000000000LL;
inline constexpr auto kQueueDepth = 5;
inline constexpr auto kPrincipalPointY = 5U;
inline constexpr auto kNanosPerMicro = 1000;
inline constexpr double kDefaultTargetTimeout = 0.35;
inline constexpr double kDefaultLossTimeout = 1.5;
inline constexpr double kMaximumLossTimeout = 5;
inline constexpr auto kMaxLabels = 512U;
inline constexpr double kDefaultMaxDisplacement = 30;
inline constexpr double kFutureTolerance = 0.1;
inline constexpr double kMaximumPitch = 1.4;
inline constexpr double kMaximumOffset = 2;

inline bool Finite(const double value) { return std::isfinite(value); }

template <class Value>
std::optional<Value> Nearest(
    const std::deque<std::pair<std::uint64_t, Value>>& history,
    const std::uint64_t timestamp) {
  const auto distance = [timestamp](const auto& sample) {
    return std::abs(static_cast<double>(sample.first) -
                    static_cast<double>(timestamp));
  };
  const auto sample = std::min_element(
      history.begin(), history.end(), [&](const auto& left, const auto& right) {
        return distance(left) < distance(right);
      });
  if (sample == history.end() || distance(*sample) > kPoseToleranceUs) {
    return std::nullopt;
  }
  return sample->second;
}

template <class Value>
void Append(std::deque<std::pair<std::uint64_t, Value>>& history,
            const std::uint64_t timestamp, const Value& value) {
  if (!history.empty() && timestamp <= history.back().first) {
    return;
  }
  history.emplace_back(timestamp, value);
  while (history.size() > kHistory) {
    history.pop_front();
  }
}

inline bool ValidCalibration(const sensor_msgs::msg::CameraInfo& info) {
  constexpr auto kMaximumDimension = 4096U;
  return info.width > 0 && info.height > 0 && info.width <= kMaximumDimension &&
         info.height <= kMaximumDimension &&
         std::all_of(info.k.begin(), info.k.end(), Finite) && info.k[0] > 0 &&
         info.k[4] > 0 &&
         std::all_of(info.d.begin(), info.d.end(),
                     [](const double value) { return value == 0; }) &&
         !info.header.frame_id.empty();
}

}  // namespace raven::guidance
