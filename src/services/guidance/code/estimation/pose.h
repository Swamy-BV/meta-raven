// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

#include "core/types.h"

namespace raven::guidance {
using PositionSamples = std::deque<std::pair<std::uint64_t, Vector>>;
using AttitudeSamples = std::deque<std::pair<std::uint64_t, Quaternion>>;

struct AlignedPose {
  Pose pose;
  double position_span_ms = 0;
  double attitude_span_ms = 0;
};

// Samples must be ordered by increasing capture time in microseconds, in the
// same clock domain as timestamp. Interpolate only within bracketing samples
// separated by at most 100 ms; never extrapolate missing vehicle motion.
[[nodiscard]] std::optional<AlignedPose> AlignPose(
    const PositionSamples& positions, const AttitudeSamples& attitudes,
    std::uint64_t timestamp);
}  // namespace raven::guidance
