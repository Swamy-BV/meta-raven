// SPDX-License-Identifier: AGPL-3.0-only
#include "go_to.h"

#include <cmath>

namespace raven::guidance {
namespace {
constexpr double kArrivalSpeed = 0.3;
}

double DistanceTo(const Vector& position, const Destination& destination) {
  return std::hypot(position[0] - destination.position[0],
                    position[1] - destination.position[1],
                    position[2] - destination.position[2]);
}

bool Arrived(const Kinematics& measured, const Destination& destination) {
  return DistanceTo(measured.position, destination) <= destination.tolerance &&
         std::hypot(measured.velocity[0], measured.velocity[1],
                    measured.velocity[2]) <= kArrivalSpeed;
}
}  // namespace raven::guidance
