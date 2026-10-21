// SPDX-License-Identifier: AGPL-3.0-only
#include "motion.h"

#include <algorithm>
#include <cmath>

#include "core/types.h"

namespace raven::guidance {
namespace {
constexpr double kMinimumTolerance = 0.1;
constexpr double kMaximumTolerance = 2;
constexpr double kMaximumTimeout = 300;
constexpr double kNumericalTolerance = 1e-6;

bool Finite(const Vector& vector) {
  return std::all_of(vector.begin(), vector.end(),
                     [](const double value) { return std::isfinite(value); });
}

double Norm(const Vector& vector) {
  return std::hypot(vector[0], vector[1], vector[2]);
}
}  // namespace

bool ValidDestination(const Destination& destination,
                      const Configuration& configuration) {
  return Finite(destination.position) && std::isfinite(destination.yaw) &&
         std::isfinite(destination.tolerance) &&
         destination.tolerance >= kMinimumTolerance &&
         destination.tolerance <= kMaximumTolerance &&
         std::isfinite(destination.timeout) && destination.timeout > 0 &&
         destination.timeout <= kMaximumTimeout &&
         std::isfinite(destination.speed) && destination.speed > 0 &&
         destination.speed <= configuration.max_speed;
}

bool ValidMotion(const Kinematics& motion, const MotionLimits& limits) {
  return Finite(motion.position) && Finite(motion.velocity) &&
         Finite(motion.acceleration) &&
         Norm(motion.velocity) <= limits.speed + kNumericalTolerance &&
         Norm(motion.acceleration) <= limits.acceleration + kNumericalTolerance;
}
}  // namespace raven::guidance
