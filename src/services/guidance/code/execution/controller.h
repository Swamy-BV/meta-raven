// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>

#include "core/types.h"
#include "planning/trajectory.h"

namespace raven::guidance {
// Shared reference execution for following and braking. Domain code has no ROS
// publishers or flight authority; the node remains the single PX4 output owner.
class MotionExecutor {
 public:
  void Reset(const Kinematics& measured, const MotionLimits& limits);
  [[nodiscard]] std::optional<Kinematics> Velocity(const Vector& desired,
                                                   double dt);

 private:
  MotionLimits limits_;
  VelocitySmoother smoother_;
};

struct HeadingStep {
  double current = 0;
  double desired = 0;
  double max_rate = 0;
  double dt = 0;
};

[[nodiscard]] double LimitYaw(const HeadingStep& step);
}  // namespace raven::guidance
