// SPDX-License-Identifier: AGPL-3.0-only
#include "trail.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

namespace raven::guidance {
namespace {
constexpr double kMinimumStep = 0.5;
constexpr double kMaximumStep = 10;
constexpr auto kMaximumPoints = 512U;
// Fit across twelve metres, above the half-metre observation spacing. Uniform
// arc-length samples avoid giving dense or slow sections extra influence.
constexpr std::array<double, 7> kDirectionOffsets{-6, -4, -2, 0, 2, 4, 6};
constexpr double kMinimumDirectionNorm = 1e-9;

struct DirectionSample {
  double offset = 0;
  Vector position{};
};

using DirectionSamples = std::array<DirectionSample, kDirectionOffsets.size()>;

double Distance(const Vector& left, const Vector& right) {
  return std::hypot(left[0] - right[0], left[1] - right[1]);
}

Vector FitDirection(const DirectionSamples& samples, const std::size_t count) {
  if (count < 2) {
    return {};
  }
  double mean_offset = 0;
  Vector mean_position{};
  for (std::size_t index = 0; index < count; ++index) {
    mean_offset += samples[index].offset;
    for (std::size_t axis = 0; axis < 2; ++axis) {
      mean_position[axis] += samples[index].position[axis];
    }
  }
  const auto weight = 1.0 / static_cast<double>(count);
  mean_offset *= weight;
  Vector direction{};
  for (std::size_t index = 0; index < count; ++index) {
    for (std::size_t axis = 0; axis < 2; ++axis) {
      direction[axis] +=
          (samples[index].offset - mean_offset) *
          (samples[index].position[axis] - mean_position[axis] * weight);
    }
  }
  const auto norm = std::hypot(direction[0], direction[1]);
  if (norm <= kMinimumDirectionNorm) {
    return {};
  }
  return {direction[0] / norm, direction[1] / norm, 0};
}
}  // namespace

void TargetTrail::Reset() { points_.clear(); }

void TargetTrail::Observe(const Motion& motion) {
  const auto finite = [](const double value) { return std::isfinite(value); };
  if (!std::all_of(motion.position.begin(), motion.position.end(), finite) ||
      !std::all_of(motion.velocity.begin(), motion.velocity.end(), finite)) {
    return;
  }
  if (!points_.empty()) {
    const auto step = Distance(points_.back().position, motion.position);
    if (step < kMinimumStep) {
      return;
    }
    if (step > kMaximumStep) {
      Reset();
    }
  }
  points_.push_back(motion);
  while (points_.size() > kMaximumPoints) {
    points_.pop_front();
  }
}

std::optional<Vector> TargetTrail::PointAt(const double distance) const {
  auto remaining = distance;
  for (std::size_t index = points_.size(); index > 1; --index) {
    const auto& ahead = points_[index - 1];
    const auto& behind = points_[index - 2];
    const auto length = Distance(ahead.position, behind.position);
    if (remaining > length) {
      remaining -= length;
      continue;
    }
    const auto fraction = remaining / length;
    Vector point{};
    for (std::size_t axis = 0; axis < point.size(); ++axis) {
      point[axis] = ahead.position[axis] +
                    fraction * (behind.position[axis] - ahead.position[axis]);
    }
    return point;
  }
  return std::nullopt;
}

std::optional<Motion> TargetTrail::Reference(const double distance,
                                             const double speed) const {
  if (!std::isfinite(distance) || distance < 0 || !std::isfinite(speed) ||
      speed < 0) {
    return std::nullopt;
  }
  const auto position = PointAt(distance);
  if (!position) {
    return std::nullopt;
  }
  if (distance == 0) {
    // Coasting may consume the recorded trail, but must not feed cruising
    // velocity beyond its last camera-observed point.
    return Motion{*position, {}};
  }
  DirectionSamples samples{};
  std::size_t count = 0;
  // Keep samples ahead of the reference inside the observed trail. At its
  // oldest edge the fit may be one-sided; it never extrapolates the trail.
  const auto scale = std::min(1.0, distance / kDirectionOffsets.back());
  for (const auto offset : kDirectionOffsets) {
    const auto along = offset * scale;
    const auto point = PointAt(std::max(0.0, distance - along));
    if (point) {
      samples[count++] = {along, *point};
    }
  }
  const auto direction = FitDirection(samples, count);
  // Preserve the exact delayed position and current target speed. Historical
  // velocities cause lag and catch-up surges during braking and acceleration.
  return Motion{*position, {direction[0] * speed, direction[1] * speed, 0}};
}
}  // namespace raven::guidance
