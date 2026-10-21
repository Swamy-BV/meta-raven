// SPDX-License-Identifier: AGPL-3.0-only
#include "trajectory.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

#include <ruckig/input_parameter.hpp>
#include <ruckig/profile.hpp>
#include <ruckig/ruckig.hpp>

namespace raven::guidance {
namespace {
constexpr double kSpatialShare = 0.5773502691896258;
constexpr double kHorizontalShare = 0.7071067811865476;
constexpr double kJerkPositionFactor = 1.0 / 6;

ruckig::InputParameter<3> Input(const Kinematics& initial,
                                const MotionLimits& limits,
                                const bool horizontal_only = false) {
  ruckig::InputParameter<3> input;
  input.current_position = initial.position;
  input.current_velocity = initial.velocity;
  input.current_acceleration = initial.acceleration;
  const auto share = horizontal_only ? kHorizontalShare : kSpatialShare;
  // Horizontal velocity targets already have a vector speed bound. Applying
  // sqrt(1/2) again rejects legal axis-aligned motion before reaching that
  // bound. CanLevelAcceleration separately enforces the norm throughout
  // transitions.
  input.max_velocity.fill(horizontal_only ? limits.speed
                                          : limits.speed * share);
  input.max_acceleration.fill(limits.acceleration * share);
  input.max_jerk.fill(limits.jerk * share);
  input.enabled[2] = !horizontal_only;
  return input;
}

bool CanLevelAcceleration(const Kinematics& state, const Vector& jerk,
                          const double speed) {
  Vector bound{};
  for (std::size_t axis = 0; axis < bound.size(); ++axis) {
    const auto acceleration = state.acceleration[axis];
    const auto after = state.velocity[axis] +
                       acceleration * std::abs(acceleration) / (2 * jerk[axis]);
    bound[axis] = std::max(std::abs(state.velocity[axis]), std::abs(after));
  }
  // Per-axis extrema bound every intermediate speed while leveling
  // acceleration.
  constexpr double kNumericalTolerance = 1e-9;
  return std::hypot(bound[0], bound[1], bound[2]) <=
         speed + kNumericalTolerance;
}

Kinematics LevelAcceleration(const ruckig::InputParameter<3>& input,
                             const double dt) {
  Kinematics state;
  for (std::size_t axis = 0; axis < state.position.size(); ++axis) {
    const auto acceleration = input.current_acceleration[axis];
    const auto duration = std::abs(acceleration) / input.max_jerk[axis];
    const auto ramp = std::min(dt, duration);
    const auto jerk = -std::copysign(input.max_jerk[axis], acceleration);
    const auto velocity = input.current_velocity[axis] + acceleration * ramp +
                          jerk * ramp * ramp / 2;
    state.position[axis] = input.current_position[axis] +
                           input.current_velocity[axis] * ramp +
                           acceleration * ramp * ramp / 2 +
                           jerk * ramp * ramp * ramp * kJerkPositionFactor +
                           velocity * (dt - ramp);
    state.velocity[axis] = velocity;
    state.acceleration[axis] = dt >= duration ? 0 : acceleration + jerk * ramp;
  }
  return state;
}
}  // namespace

bool Trajectory::Plan(const Kinematics& initial, const Vector& target,
                      const MotionLimits& limits) {
  auto input = Input(initial, limits);
  input.target_position = target;
  ruckig::Ruckig<3> generator;
  return generator.validate_input(input, true, true) &&
         generator.calculate(input, trajectory_) == ruckig::Result::Working;
}

Kinematics Trajectory::Sample(const double elapsed) const {
  Kinematics sample;
  trajectory_.at_time(std::clamp(elapsed, 0.0, Duration()), sample.position,
                      sample.velocity, sample.acceleration);
  return sample;
}

double Trajectory::Duration() const { return trajectory_.get_duration(); }

bool Trajectory::Inside(const Vector& origin, const double radius,
                        const double minimum_z, const double maximum_z) {
  // The exact per-axis extrema bound the entire trajectory, including braking
  // overshoot. Their enclosing XY rectangle conservatively bounds its radius.
  const auto bounds = trajectory_.get_position_extrema();
  const auto north = std::max(std::abs(bounds[0].min - origin[0]),
                              std::abs(bounds[0].max - origin[0]));
  const auto east = std::max(std::abs(bounds[1].min - origin[1]),
                             std::abs(bounds[1].max - origin[1]));
  return std::hypot(north, east) <= radius && bounds[2].min >= minimum_z &&
         bounds[2].max <= maximum_z;
}

void VelocitySmoother::Reset(const Kinematics& initial,
                             const MotionLimits& limits) {
  speed_limit_ = limits.speed;
  input_ = Input(initial, limits, limits.horizontal_velocity);
  input_.control_interface = ruckig::ControlInterface::Velocity;
  generator_.reset();
}

std::optional<Kinematics> VelocitySmoother::Step(const Vector& velocity,
                                                 const double dt) {
  if (!std::isfinite(dt) || dt <= 0) {
    return std::nullopt;
  }
  // Ruckig can return a boundary acceleration one ULP above its own limit.
  // Normalize only numerical residue before strict validation; reject larger
  // violations rather than hiding an invalid reference state.
  constexpr double kBoundaryTolerance = 1e-12;
  for (std::size_t axis = 0; axis < input_.current_acceleration.size();
       ++axis) {
    const auto limit = input_.max_acceleration[axis];
    auto& acceleration = input_.current_acceleration[axis];
    if (std::abs(acceleration) > limit + kBoundaryTolerance) {
      return std::nullopt;
    }
    acceleration = std::clamp(acceleration, -limit, limit);
  }
  input_.target_velocity = velocity;
  generator_.delta_time = dt;
  if (!generator_.validate_input(input_, true, true)) {
    return std::nullopt;
  }
  const auto result = generator_.update(input_, output_);
  if (result != ruckig::Result::Working && result != ruckig::Result::Finished) {
    return std::nullopt;
  }
  const Kinematics sample{output_.new_position, output_.new_velocity,
                          output_.new_acceleration};
  if (!CanLevelAcceleration(sample, input_.max_jerk, speed_limit_)) {
    const auto governed = LevelAcceleration(input_, dt);
    input_.current_position = governed.position;
    input_.current_velocity = governed.velocity;
    input_.current_acceleration = governed.acceleration;
    generator_.reset();
    return governed;
  }
  output_.pass_to_input(input_);
  return sample;
}
}  // namespace raven::guidance
