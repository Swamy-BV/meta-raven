// SPDX-License-Identifier: AGPL-3.0-only
#include "orbit.h"

#include <algorithm>
#include <cmath>

namespace raven::guidance {
namespace {
constexpr double kRevolution = 6.283185307179586;
constexpr double kMinimumRadius = 2;
// Maxima for the normalized quintic phase and its first three derivatives.
constexpr double kPeakRate = 1.875 * kRevolution;
constexpr double kPeakAcceleration = 6 * kRevolution;
constexpr double kPeakJerk = 60 * kRevolution;
constexpr double kQuinticCubic = 10;
constexpr double kQuinticQuartic = 15;
constexpr double kQuinticQuintic = 6;
constexpr double kDerivativeScale = 30;
constexpr double kSecondDerivativeScale = 60;

bool Finite(const Vector& vector) {
  return std::all_of(vector.begin(), vector.end(),
                     [](const double value) { return std::isfinite(value); });
}
}  // namespace

bool OrbitPlan::Plan(const OrbitRequest& request, const MotionLimits& limits) {
  duration_ = 0;
  if (!Finite(request.center) || !Finite(request.initial) ||
      !std::isfinite(request.radius) || request.radius < kMinimumRadius ||
      !std::isfinite(request.speed) || request.speed <= 0 ||
      !std::isfinite(request.yaw_rate) || request.yaw_rate <= 0 ||
      !std::isfinite(limits.speed) || request.speed > limits.speed ||
      !std::isfinite(limits.acceleration) || limits.acceleration <= 0 ||
      !std::isfinite(limits.jerk) || limits.jerk <= 0 ||
      std::hypot(request.initial[0] - request.center[0],
                 request.initial[1] - request.center[1]) < kMinimumRadius) {
    return false;
  }
  center_ = request.center;
  height_ = request.initial[2];
  radius_ = request.radius;
  direction_ = request.clockwise ? 1 : -1;
  start_angle_ = OrbitAzimuth(request.initial, center_);
  // Triangle inequalities deliberately overestimate the acceleration and jerk
  // norms of a circle with nonuniform angular speed.
  duration_ = std::max(
      {radius_ * kPeakRate / request.speed, kPeakRate / request.yaw_rate,
       std::sqrt(radius_ * (kPeakAcceleration + kPeakRate * kPeakRate) /
                 limits.acceleration),
       std::cbrt(radius_ *
                 (kPeakJerk + 3 * kPeakRate * kPeakAcceleration +
                  kPeakRate * kPeakRate * kPeakRate) /
                 limits.jerk)});
  return std::isfinite(duration_) && duration_ > 0;
}

Kinematics OrbitPlan::Sample(const double elapsed) const {
  const auto u = std::clamp(elapsed / duration_, 0.0, 1.0);
  const auto phase =
      u * u * u *
      (kQuinticCubic - kQuinticQuartic * u + kQuinticQuintic * u * u);
  const auto rate = direction_ * kRevolution * kDerivativeScale * u * u *
                    (1 - u) * (1 - u) / duration_;
  const auto acceleration = direction_ * kRevolution * kSecondDerivativeScale *
                            u * (1 - u) * (1 - 2 * u) / (duration_ * duration_);
  const auto angle = start_angle_ + direction_ * kRevolution * phase;
  const auto north = radius_ * std::cos(angle);
  const auto east = radius_ * std::sin(angle);
  return {{center_[0] + north, center_[1] + east, height_},
          {-east * rate, north * rate, 0},
          {-north * rate * rate - east * acceleration,
           -east * rate * rate + north * acceleration, 0}};
}

double OrbitPlan::Duration() const { return duration_; }

const Vector& OrbitPlan::Center() const { return center_; }

double OrbitPlan::Radius() const { return radius_; }

double OrbitPlan::Direction() const { return direction_; }

double OrbitAzimuth(const Vector& position, const Vector& center) {
  return std::atan2(position[1] - center[1], position[0] - center[0]);
}

double AngleDifference(const double to, const double from) {
  return std::atan2(std::sin(to - from), std::cos(to - from));
}
}  // namespace raven::guidance
