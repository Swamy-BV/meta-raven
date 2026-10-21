// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>

#include "core/types.h"

namespace raven::guidance {
struct FollowInput {
  Pose pose;
  Vector target{};
  Vector target_velocity{};
  Vector previous_velocity{};
  double previous_yaw = 0;
  double dt = 0;
  double previous_bearing = 0;
  std::optional<Motion> reference;
};

struct Command {
  Vector velocity{};
  double yaw = 0;
  double bearing = 0;
  double yaw_rate = 0;  // Derivative of the commanded heading, radians/second.
};

[[nodiscard]] bool ValidPose(const Pose& pose);

// Assumes the box bottom centre touches a known level ground plane. This is
// unsuitable for elevated targets, uneven terrain or unrectified pixels.
[[nodiscard]] std::optional<Vector> ProjectGround(const Pixel& pixel,
                                                  const Camera& camera,
                                                  const Pose& pose,
                                                  const Configuration& config);
[[nodiscard]] Command Follow(const FollowInput& input,
                             const Configuration& config);
[[nodiscard]] double Heading(const Quaternion& orientation);
}  // namespace raven::guidance
