// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "behaviors/go_to.h"
#include "core/types.h"
#include "planning/trajectory.h"

namespace raven::guidance {
[[nodiscard]] bool ValidDestination(const Destination& destination,
                                    const Configuration& configuration);
[[nodiscard]] bool ValidMotion(const Kinematics& motion,
                               const MotionLimits& limits);
}  // namespace raven::guidance
