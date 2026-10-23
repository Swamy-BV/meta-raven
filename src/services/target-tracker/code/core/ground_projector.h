// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "core/ground_motion.h"
#include "core/tracker.h"

namespace raven::tracking {
inline constexpr double kDefaultGroundRange = 80;

struct GroundCamera {
  double ground_z = 0;
  double pitch = 0;
  double max_range = kDefaultGroundRange;
  std::array<double, 3> offset{};
  bool stabilized = false;
};

struct GroundCalibration {
  std::string frame;
  double fx = 0, fy = 0, cx = 0, cy = 0;
  unsigned int width = 0, height = 0;
};

// Uses bracketing body-FRD-to-local-NED pose samples, with no extrapolation.
// The caller clears history on navigation-origin, attitude or calibration
// reset.
class GroundProjector final {
 public:
  explicit GroundProjector(GroundCamera camera);
  bool Calibrate(const GroundCalibration& calibration);
  void Position(std::int64_t stamp, const std::array<double, 3>& position);
  void Attitude(std::int64_t stamp, const std::array<double, 4>& orientation);
  [[nodiscard]] std::optional<GroundPoint> Project(const Box& box,
                                                   const std::string& frame,
                                                   std::int64_t stamp) const;
  void Reset();
  void InvalidateCalibration();

 private:
  GroundCamera camera_;
  std::optional<GroundCalibration> calibration_;
  std::deque<std::pair<std::int64_t, std::array<double, 3>>> positions_;
  std::deque<std::pair<std::int64_t, std::array<double, 4>>> attitudes_;
};
}  // namespace raven::tracking
