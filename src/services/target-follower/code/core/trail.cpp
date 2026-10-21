// SPDX-License-Identifier: AGPL-3.0-only
#include "trail.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

#include "core/follower.h"

namespace raven::following {
namespace {
constexpr double kMinimumStep = 0.5;
constexpr double kMaximumStep = 10;
constexpr auto kMaximumPoints = 512U;

double Distance(const Vector& left, const Vector& right) {
  return std::hypot(left[0] - right[0], left[1] - right[1]);
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

std::optional<Motion> TargetTrail::Reference(const double distance) const {
  if (!std::isfinite(distance) || distance <= 0) {
    return std::nullopt;
  }
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
    Motion reference;
    for (std::size_t axis = 0; axis < reference.position.size(); ++axis) {
      reference.position[axis] =
          ahead.position[axis] +
          fraction * (behind.position[axis] - ahead.position[axis]);
      reference.velocity[axis] =
          ahead.velocity[axis] +
          fraction * (behind.velocity[axis] - ahead.velocity[axis]);
    }
    return reference;
  }
  return std::nullopt;
}
}  // namespace raven::following
