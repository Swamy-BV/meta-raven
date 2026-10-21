// SPDX-License-Identifier: AGPL-3.0-only
#include "controller.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "validation/motion.h"

namespace raven::guidance {
void MotionExecutor::Reset(const Kinematics& measured,
                           const MotionLimits& limits) {
  limits_ = limits;
  smoother_.Reset(measured, limits);
}

std::optional<Kinematics> MotionExecutor::Velocity(const Vector& desired,
                                                   const double dt) {
  const auto sample = smoother_.Step(desired, dt);
  if (!sample || !ValidMotion(*sample, limits_)) {
    return std::nullopt;
  }
  return sample;
}

double LimitYaw(const HeadingStep& step) {
  const auto error = std::atan2(std::sin(step.desired - step.current),
                                std::cos(step.desired - step.current));
  return step.current +
         std::clamp(error, -step.max_rate * step.dt, step.max_rate * step.dt);
}
}  // namespace raven::guidance
