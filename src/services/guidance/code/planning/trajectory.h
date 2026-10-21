// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>

#include <ruckig/input_parameter.hpp>
#include <ruckig/output_parameter.hpp>
#include <ruckig/ruckig.hpp>
#include <ruckig/trajectory.hpp>

#include "core/types.h"

namespace raven::guidance {
struct Kinematics {
  Vector position{};
  Vector velocity{};
  Vector acceleration{};
};

// Each axis receives a conservative share of the vector-norm limits. No cloud
// client, intermediate waypoints or obstacle avoidance is used.
struct MotionLimits {
  double speed = 0;
  double acceleration = 0;
  double jerk = 0;
  // Reserve only XY velocity-control budget; position trajectories remain 3D.
  bool horizontal_velocity = false;
};

class Trajectory {
 public:
  [[nodiscard]] bool Plan(const Kinematics& initial, const Vector& target,
                          const MotionLimits& limits);
  [[nodiscard]] Kinematics Sample(double elapsed) const;
  [[nodiscard]] double Duration() const;
  [[nodiscard]] bool Inside(const Vector& origin, double radius,
                            double minimum_z, double maximum_z);

 private:
  ruckig::Trajectory<3> trajectory_;
};

// Bounded, allocation-free state-to-state velocity generation. Call Reset only
// on ownership handover, using measured motion; successive updates preserve
// jerk. Before reaching the speed boundary, level acceleration with bounded
// jerk so a direction change cannot require an unavoidable speed overshoot.
class VelocitySmoother {
 public:
  void Reset(const Kinematics& initial, const MotionLimits& limits);
  [[nodiscard]] std::optional<Kinematics> Step(const Vector& velocity,
                                               double dt);

 private:
  double speed_limit_ = 0;
  ruckig::Ruckig<3> generator_;
  ruckig::InputParameter<3> input_;
  ruckig::OutputParameter<3> output_;
};
}  // namespace raven::guidance
