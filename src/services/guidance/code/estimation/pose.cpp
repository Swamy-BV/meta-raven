// SPDX-License-Identifier: AGPL-3.0-only
#include "pose.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>

#include "core/types.h"

namespace raven::guidance {
namespace {
constexpr std::uint64_t kMaximumSpanUs = 100000;
constexpr double kMicrosPerMillisecond = 1000;
constexpr double kQuaternionTolerance = 0.01;
constexpr double kLinearRotationThreshold = 0.9995;

struct Bracket {
  std::size_t before = 0;
  std::size_t after = 0;
  double fraction = 0;
  double span_ms = 0;
};

template <class Samples>
std::optional<Bracket> FindBracket(const Samples& samples,
                                   const std::uint64_t timestamp) {
  const auto after =
      std::lower_bound(samples.begin(), samples.end(), timestamp,
                       [](const auto& sample, const std::uint64_t stamp) {
                         return sample.first < stamp;
                       });
  if (after == samples.end()) {
    return std::nullopt;
  }
  const auto index =
      static_cast<std::size_t>(std::distance(samples.begin(), after));
  if (after->first == timestamp) {
    return Bracket{index, index, 0, 0};
  }
  if (after == samples.begin()) {
    return std::nullopt;
  }
  const auto before = std::prev(after);
  const auto span = after->first - before->first;
  if (span == 0 || span > kMaximumSpanUs) {
    return std::nullopt;
  }
  return Bracket{index - 1, index,
                 static_cast<double>(timestamp - before->first) /
                     static_cast<double>(span),
                 static_cast<double>(span) / kMicrosPerMillisecond};
}

std::optional<Quaternion> Normalize(Quaternion orientation) {
  double squared_norm = 0;
  for (const auto value : orientation) {
    squared_norm += value * value;
  }
  if (!std::isfinite(squared_norm) ||
      std::abs(squared_norm - 1) > kQuaternionTolerance) {
    return std::nullopt;
  }
  const auto norm = std::sqrt(squared_norm);
  for (auto& value : orientation) {
    value /= norm;
  }
  return orientation;
}

std::optional<Quaternion> InterpolateOrientation(const AttitudeSamples& samples,
                                                 const Bracket& bracket) {
  const auto first = Normalize(samples[bracket.before].second);
  auto last = Normalize(samples[bracket.after].second);
  if (!first || !last) {
    return std::nullopt;
  }
  double dot = 0;
  for (std::size_t axis = 0; axis < first->size(); ++axis) {
    dot += (*first)[axis] * (*last)[axis];
  }
  // q and -q describe the same rotation. Choose the shorter arc, including
  // headings that straddle +/- pi, rather than rotating the long way around.
  if (dot < 0) {
    for (auto& value : *last) {
      value = -value;
    }
    dot = -dot;
  }
  dot = std::clamp(dot, 0.0, 1.0);
  auto first_weight = 1 - bracket.fraction;
  auto last_weight = bracket.fraction;
  if (dot < kLinearRotationThreshold) {
    const auto angle = std::acos(dot);
    const auto sine = std::sin(angle);
    first_weight = std::sin((1 - bracket.fraction) * angle) / sine;
    last_weight = std::sin(bracket.fraction * angle) / sine;
  }
  Quaternion result{};
  for (std::size_t axis = 0; axis < result.size(); ++axis) {
    result[axis] = first_weight * (*first)[axis] + last_weight * (*last)[axis];
  }
  return Normalize(result);
}
}  // namespace

std::optional<AlignedPose> AlignPose(const PositionSamples& positions,
                                     const AttitudeSamples& attitudes,
                                     const std::uint64_t timestamp) {
  const auto position = FindBracket(positions, timestamp);
  const auto attitude = FindBracket(attitudes, timestamp);
  if (!position || !attitude) {
    return std::nullopt;
  }
  const auto orientation = InterpolateOrientation(attitudes, *attitude);
  if (!orientation) {
    return std::nullopt;
  }
  Vector location{};
  for (std::size_t axis = 0; axis < location.size(); ++axis) {
    const auto before = positions[position->before].second[axis];
    const auto after = positions[position->after].second[axis];
    location[axis] = before + position->fraction * (after - before);
    if (!std::isfinite(location[axis])) {
      return std::nullopt;
    }
  }
  return AlignedPose{
      {location, *orientation}, position->span_ms, attitude->span_ms};
}
}  // namespace raven::guidance
