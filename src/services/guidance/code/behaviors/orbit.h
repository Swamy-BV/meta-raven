// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "core/types.h"
#include "planning/trajectory.h"

namespace raven::guidance {
struct OrbitRequest {
  Vector center{};  // Frozen local NED object centre, metres.
  Vector initial{};
  double radius = 0;
  double speed = 0;
  double yaw_rate = 0;
  bool clockwise = true;  // Viewed from above; positive NED azimuth.
};

// A single horizontal revolution with zero velocity/acceleration at each end.
// A quintic phase bounds tangential and centripetal acceleration and jerk;
// wall time determines the reference, never whether the aircraft has arrived.
class OrbitPlan {
 public:
  [[nodiscard]] bool Plan(const OrbitRequest& request,
                          const MotionLimits& limits);
  [[nodiscard]] Kinematics Sample(double elapsed) const;
  [[nodiscard]] double Duration() const;
  [[nodiscard]] const Vector& Center() const;
  [[nodiscard]] double Radius() const;
  [[nodiscard]] double Direction() const;

 private:
  Vector center_{};
  double height_ = 0;
  double radius_ = 0;
  double start_angle_ = 0;
  double direction_ = 1;
  double duration_ = 0;
};

// Signed shortest angular increment, in radians, about a frozen NED centre.
[[nodiscard]] double OrbitAzimuth(const Vector& position, const Vector& center);
[[nodiscard]] double AngleDifference(double to, double from);
}  // namespace raven::guidance
