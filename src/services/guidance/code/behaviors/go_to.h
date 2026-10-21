// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/types.h"
#include "planning/trajectory.h"

namespace raven::guidance {
struct Destination {
  Vector position{};
  double yaw = 0;
  double tolerance = 0;
  double timeout = 0;
  double speed = 0;
};

[[nodiscard]] double DistanceTo(const Vector& position,
                                const Destination& destination);
// Completion is based on measured arrival and low speed, not just elapsed plan
// time or command acceptance.
[[nodiscard]] bool Arrived(const Kinematics& measured,
                           const Destination& destination);
}  // namespace raven::guidance
