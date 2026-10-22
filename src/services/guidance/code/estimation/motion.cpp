// SPDX-License-Identifier: AGPL-3.0-only
#include "motion.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>

#include "core/types.h"

namespace raven::guidance {
namespace {
constexpr double kMaximumPrediction = 0.35;
constexpr double kMinimumMotionSpan = 0.4;
constexpr double kInnovationGate = 9.210340371976184;

bool Finite(const Vector& point) {
  return std::all_of(point.begin(), point.end(),
                     [](const double value) { return std::isfinite(value); });
}
}  // namespace

MotionEstimator::MotionEstimator(const double position_stddev,
                                 const double acceleration_noise,
                                 const double retention_seconds)
    : measurement_variance_(position_stddev * position_stddev),
      acceleration_noise_(acceleration_noise),
      retention_seconds_(retention_seconds) {
  if (!std::isfinite(position_stddev) || position_stddev <= 0 ||
      !std::isfinite(measurement_variance_) || measurement_variance_ <= 0 ||
      !std::isfinite(acceleration_noise) || acceleration_noise <= 0 ||
      !std::isfinite(retention_seconds) || retention_seconds <= 0) {
    throw std::invalid_argument("Invalid target motion configuration");
  }
}

void MotionEstimator::Reset() {
  motion_.reset();
  innovation_.reset();
  covariance_ = {};
  stamp_ = 0;
  first_stamp_ = 0;
}

MotionEstimator::Covariance MotionEstimator::PredictCovariance(
    const double dt) const {
  return {covariance_.position + 2 * dt * covariance_.cross +
              dt * dt * covariance_.velocity +
              acceleration_noise_ * dt * dt * dt / 3,
          covariance_.cross + dt * covariance_.velocity +
              acceleration_noise_ * dt * dt / 2,
          covariance_.velocity + acceleration_noise_ * dt};
}

bool MotionEstimator::Observe(const Vector& point, const double stamp) {
  innovation_.reset();
  if (!std::isfinite(stamp) || !Finite(point) || (motion_ && stamp <= stamp_)) {
    return false;
  }
  if (motion_ && stamp - stamp_ > retention_seconds_) {
    Reset();
  }
  if (!motion_) {
    motion_ = Motion{point, {0, 0, 0}};
    covariance_.position = measurement_variance_;
    stamp_ = stamp;
    first_stamp_ = stamp;
    return true;
  }
  const auto dt = stamp - stamp_;
  auto prediction = *motion_;
  prediction.position[0] += dt * prediction.velocity[0];
  prediction.position[1] += dt * prediction.velocity[1];
  const auto covariance = PredictCovariance(dt);
  const auto north = point[0] - prediction.position[0];
  const auto east = point[1] - prediction.position[1];
  const auto variance = covariance.position + measurement_variance_;
  const auto ratio = (north * north + east * east) / variance / kInnovationGate;
  innovation_ = MotionInnovation{{north, east, 0}, stamp, ratio, ratio <= 1};
  if (!innovation_->accepted) {
    return false;
  }
  Correct(point, prediction, covariance, stamp);
  return true;
}

const std::optional<MotionInnovation>& MotionEstimator::LastInnovation() const {
  return innovation_;
}

void MotionEstimator::Correct(const Vector& point, const Motion& prediction,
                              const Covariance& covariance,
                              const double stamp) {
  const auto variance = covariance.position + measurement_variance_;
  const auto position_gain = covariance.position / variance;
  const auto velocity_gain = covariance.cross / variance;
  motion_ = prediction;
  constexpr auto kHorizontalAxes = 2U;
  for (std::size_t axis = 0; axis < kHorizontalAxes; ++axis) {
    const auto innovation = point[axis] - prediction.position[axis];
    motion_->position[axis] += position_gain * innovation;
    motion_->velocity[axis] += velocity_gain * innovation;
  }
  motion_->position[2] = point[2];
  // Joseph covariance update avoids cancellation near converged measurements.
  const auto retained = 1 - position_gain;
  covariance_ = {
      retained * retained * covariance.position +
          position_gain * position_gain * measurement_variance_,
      retained * (covariance.cross - velocity_gain * covariance.position) +
          position_gain * velocity_gain * measurement_variance_,
      covariance.velocity - 2 * velocity_gain * covariance.cross +
          velocity_gain * velocity_gain * variance};
  stamp_ = stamp;
}

std::optional<Motion> MotionEstimator::Estimate(const double stamp,
                                                const double max_speed) const {
  if (!motion_ || !std::isfinite(stamp) || !std::isfinite(max_speed) ||
      max_speed <= 0 || stamp < stamp_ || stamp - stamp_ > kMaximumPrediction) {
    return std::nullopt;
  }
  auto result = *motion_;
  const auto speed = std::hypot(result.velocity[0], result.velocity[1]);
  const auto scale = speed > max_speed ? max_speed / speed : 1.0;
  for (std::size_t axis = 0; axis < result.position.size(); ++axis) {
    result.velocity[axis] = stamp_ - first_stamp_ >= kMinimumMotionSpan
                                ? result.velocity[axis] * scale
                                : 0;
    result.position[axis] += result.velocity[axis] * (stamp - stamp_);
  }
  return result;
}
}  // namespace raven::guidance
