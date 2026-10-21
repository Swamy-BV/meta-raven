// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>

#include "core/types.h"

namespace raven::guidance {
inline constexpr double kTargetPositionStddev = 0.35;
inline constexpr double kTargetAccelerationNoise = 0.5;

struct MotionInnovation {
  Vector residual{};      // North/east measurement minus prediction, metres.
  double stamp = 0;       // Measurement time, seconds in the observation clock.
  double test_ratio = 0;  // Squared Mahalanobis distance / rejection threshold.
  bool accepted = false;
};

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
  [[nodiscard]] const std::optional<MotionInnovation>& LastInnovation() const;
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
  std::optional<MotionInnovation> innovation_;
};
}  // namespace raven::guidance
