// SPDX-License-Identifier: AGPL-3.0-only
#include "ground_motion.h"

#include <cmath>
#include <cstddef>
#include <optional>

namespace raven::tracking {
namespace {
constexpr double kMeasurementVariance = 0.5625;
constexpr double kAccelerationNoise = 3.0;
constexpr double kInnovationGate = 9.210340371976184;
constexpr double kMaximumResidual = 6.0;
constexpr double kInitialVelocityVariance = 25.0;
constexpr double kMaximumRetention = 10.0;

bool Valid(const GroundPoint& point, double stamp, double retention) {
  return std::isfinite(point[0]) && std::isfinite(point[1]) &&
         std::isfinite(stamp) && stamp > 0 && std::isfinite(retention) &&
         retention > 0 && retention <= kMaximumRetention;
}
}  // namespace

GroundMotion::Prediction GroundMotion::Predict(double dt) const {
  auto position = position_;
  for (std::size_t axis = 0; axis < position.size(); ++axis) {
    position[axis] += velocity_[axis] * dt;
  }
  return {position,
          {covariance_.position + 2 * dt * covariance_.cross +
               dt * dt * covariance_.velocity +
               kAccelerationNoise * dt * dt * dt / 3,
           covariance_.cross + dt * covariance_.velocity +
               kAccelerationNoise * dt * dt / 2,
           covariance_.velocity + kAccelerationNoise * dt}};
}

void GroundMotion::Initialize(const GroundPoint& point, double stamp) {
  position_ = point;
  velocity_ = {};
  covariance_ = {kMeasurementVariance, 0, kInitialVelocityVariance};
  stamp_ = stamp;
}

std::optional<double> GroundMotion::Distance(const GroundPoint& point,
                                             double stamp,
                                             double retention) const {
  if (!Valid(point, stamp, retention) || !stamp_ || stamp < *stamp_ ||
      stamp - *stamp_ > retention) {
    return std::nullopt;
  }
  const auto prediction = Predict(stamp - *stamp_);
  const auto north = point[0] - prediction.position[0];
  const auto east = point[1] - prediction.position[1];
  const auto variance = prediction.covariance.position + kMeasurementVariance;
  const auto distance = std::hypot(north, east);
  const auto mahalanobis = (north * north + east * east) / variance;
  if (!std::isfinite(distance) || !std::isfinite(mahalanobis) ||
      distance > kMaximumResidual || mahalanobis > kInnovationGate) {
    return std::nullopt;
  }
  return distance / kMaximumResidual;
}

bool GroundMotion::Observe(const GroundPoint& point, double stamp,
                           double retention) {
  if (!Valid(point, stamp, retention) || (stamp_ && stamp <= *stamp_)) {
    return false;
  }
  if (!stamp_ || stamp - *stamp_ > retention) {
    Initialize(point, stamp);
    return true;
  }
  if (!Distance(point, stamp, retention)) {
    return false;
  }
  const auto prediction = Predict(stamp - *stamp_);
  const auto& covariance = prediction.covariance;
  const auto variance = covariance.position + kMeasurementVariance;
  const auto position_gain = covariance.position / variance;
  const auto velocity_gain = covariance.cross / variance;
  for (std::size_t axis = 0; axis < position_.size(); ++axis) {
    const auto innovation = point[axis] - prediction.position[axis];
    position_[axis] = prediction.position[axis] + position_gain * innovation;
    velocity_[axis] += velocity_gain * innovation;
  }
  // Joseph update preserves covariance positivity near convergence.
  const auto retained = 1 - position_gain;
  covariance_ = {
      retained * retained * covariance.position +
          position_gain * position_gain * kMeasurementVariance,
      retained * (covariance.cross - velocity_gain * covariance.position) +
          position_gain * velocity_gain * kMeasurementVariance,
      covariance.velocity - 2 * velocity_gain * covariance.cross +
          velocity_gain * velocity_gain * variance};
  stamp_ = stamp;
  return true;
}
}  // namespace raven::tracking
