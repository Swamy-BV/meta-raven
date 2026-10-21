// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>

#include "core/types.h"

namespace raven::guidance {
inline constexpr double kTargetPositionStddev = 0.35;
inline constexpr double kTargetAccelerationNoise = 0.5;

// Constant-velocity Kalman filter in local NED. Measurement deviation is in
// metres; continuous white-acceleration spectral density is in m^2/s^3.
// Rejected innovations do not renew source freshness or change identity.
class MotionEstimator {
 public:
  explicit MotionEstimator(
      double position_stddev = kTargetPositionStddev,
      double acceleration_noise = kTargetAccelerationNoise);
  void Reset();
  bool Observe(const Vector& point, double stamp);
  [[nodiscard]] std::optional<Motion> Estimate(double stamp,
                                               double max_speed) const;

 private:
  struct Covariance {
    double position = 0;
    double cross = 0;
    double velocity = 4;
  };

  [[nodiscard]] Covariance PredictCovariance(double dt) const;
  void Correct(const Vector& point, const Motion& prediction,
               const Covariance& covariance, double stamp);
  double measurement_variance_ = 0;
  double acceleration_noise_ = 0;
  double stamp_ = 0;
  double first_stamp_ = 0;
  Covariance covariance_;
  std::optional<Motion> motion_;
};
}  // namespace raven::guidance
