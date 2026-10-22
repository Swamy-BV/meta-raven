// SPDX-License-Identifier: AGPL-3.0-only
#include "follow.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

namespace raven::guidance {
namespace {
constexpr double kMinimumRayDown = 0.05;
constexpr double kMinimumRange = 0.2;
constexpr double kQuaternionTolerance = 0.01;
constexpr double kFormationResponseSeconds = 0.7;

Vector LimitVelocity(const Vector& velocity, const double maximum) {
  const auto speed = std::hypot(velocity[0], velocity[1]);
  const auto scale = speed > maximum ? maximum / speed : 1.0;
  return {velocity[0] * scale, velocity[1] * scale, 0};
}

double FormationBearing(const FollowInput& input, const Configuration& config) {
  const auto north = input.target[0] - input.pose.position[0];
  const auto east = input.target[1] - input.pose.position[1];
  const auto speed =
      std::hypot(input.target_velocity[0], input.target_velocity[1]);
  if (!config.align_with_motion) {
    return std::atan2(east, north);
  }
  if (speed < config.minimum_heading_speed) {
    // Keep the established formation when motion is too small to give a
    // reliable travel direction, including stops and the initial acquisition.
    return input.previous_bearing;
  }
  const auto desired =
      std::atan2(input.target_velocity[1], input.target_velocity[0]) +
      config.motion_bearing_offset;
  const auto error = std::atan2(std::sin(desired - input.previous_bearing),
                                std::cos(desired - input.previous_bearing));
  // A proportional heading response avoids bang-bang lateral feed-forward
  // from small velocity-estimate direction changes.
  const auto rate = std::clamp(error / kFormationResponseSeconds,
                               -config.max_yaw_rate, config.max_yaw_rate);
  return input.previous_bearing + rate * input.dt;
}

Motion FormationReference(const FollowInput& input, const Configuration& config,
                          const double bearing) {
  const auto rate = config.align_with_motion && input.dt > 0
                        ? (bearing - input.previous_bearing) / input.dt
                        : 0;
  const auto north = config.distance * std::cos(bearing);
  const auto east = config.distance * std::sin(bearing);
  // Differentiate target - distance * heading, including the rotating offset.
  return {{input.target[0] - north, input.target[1] - east, config.ground_z},
          {input.target_velocity[0] + east * rate,
           input.target_velocity[1] - north * rate, 0}};
}

Vector GovernSeparation(const Vector& desired, const FollowInput& input,
                        const Configuration& config) {
  const auto north = input.target[0] - input.pose.position[0];
  const auto east = input.target[1] - input.pose.position[1];
  const auto distance = std::hypot(north, east);
  if (distance < kMinimumRange) {
    return {};
  }
  const auto unit_north = north / distance;
  const auto unit_east = east / distance;
  const auto target_rate = input.target_velocity[0] * unit_north +
                           input.target_velocity[1] * unit_east;
  const auto maximum_rate =
      target_rate + config.gain * (distance - config.distance);
  const auto approach = desired[0] * unit_north + desired[1] * unit_east;
  const auto excess = std::max(0.0, approach - maximum_rate);
  return LimitVelocity(
      {desired[0] - excess * unit_north, desired[1] - excess * unit_east, 0},
      config.max_speed);
}

Vector Rotate(const Quaternion& q, const Vector& v) {
  const Vector cross{q[2] * v[2] - q[3] * v[1], q[3] * v[0] - q[1] * v[2],
                     q[1] * v[1] - q[2] * v[0]};
  return {v[0] + 2 * (q[0] * cross[0] + q[2] * cross[2] - q[3] * cross[1]),
          v[1] + 2 * (q[0] * cross[1] + q[3] * cross[0] - q[1] * cross[2]),
          v[2] + 2 * (q[0] * cross[2] + q[1] * cross[1] - q[2] * cross[0])};
}

}  // namespace

bool ValidPose(const Pose& pose) {
  const auto finite = [](const double value) { return std::isfinite(value); };
  double squared_norm = 0;
  for (const auto value : pose.orientation) {
    squared_norm += value * value;
  }
  return std::all_of(pose.position.begin(), pose.position.end(), finite) &&
         std::all_of(pose.orientation.begin(), pose.orientation.end(),
                     finite) &&
         std::abs(squared_norm - 1) < kQuaternionTolerance;
}

std::optional<Vector> ProjectGround(const Pixel& pixel, const Camera& camera,
                                    const Pose& pose,
                                    const Configuration& config) {
  if (!ValidPose(pose) || !std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
      !std::isfinite(camera.fx) || !std::isfinite(camera.fy) ||
      !std::isfinite(camera.cx) || !std::isfinite(camera.cy) ||
      !std::isfinite(camera.pitch) ||
      !std::all_of(camera.offset.begin(), camera.offset.end(),
                   [](const double value) { return std::isfinite(value); }) ||
      camera.fx <= 0 || camera.fy <= 0) {
    return std::nullopt;
  }
  const auto right = (pixel.x - camera.cx) / camera.fx;
  const auto down = (pixel.y - camera.cy) / camera.fy;
  const auto cosine = std::cos(camera.pitch);
  const auto sine = std::sin(camera.pitch);
  const Vector body_ray{cosine - sine * down, right, sine + cosine * down};
  const auto yaw = Heading(pose.orientation);
  const Quaternion camera_orientation =
      camera.stabilized ? Quaternion{std::cos(yaw / 2), 0, 0, std::sin(yaw / 2)}
                        : pose.orientation;
  const auto ray = Rotate(camera_orientation, body_ray);
  const auto offset = Rotate(pose.orientation, camera.offset);
  Vector origin{};
  for (std::size_t index = 0; index < origin.size(); ++index) {
    origin[index] = pose.position[index] + offset[index];
  }
  if (ray[2] <= kMinimumRayDown || config.ground_z <= origin[2]) {
    return std::nullopt;
  }
  const auto scale = (config.ground_z - origin[2]) / ray[2];
  const Vector target{origin[0] + scale * ray[0], origin[1] + scale * ray[1],
                      config.ground_z};
  const auto range =
      std::hypot(target[0] - pose.position[0], target[1] - pose.position[1]);
  if (!std::isfinite(range) || range < kMinimumRange ||
      range > config.max_range) {
    return std::nullopt;
  }
  return target;
}

double Heading(const Quaternion& orientation) {
  return std::atan2(
      2 * (orientation[0] * orientation[3] + orientation[1] * orientation[2]),
      1 - 2 * (orientation[2] * orientation[2] +
               orientation[3] * orientation[3]));
}

Command Follow(const FollowInput& input, const Configuration& config) {
  const auto north = input.target[0] - input.pose.position[0];
  const auto east = input.target[1] - input.pose.position[1];
  const auto bearing = FormationBearing(input, config);
  auto reference =
      input.reference.value_or(FormationReference(input, config, bearing));
  if (input.reference) {
    // A delayed path reference must not carry cruising speed into the leader's
    // current braking manoeuvre.
    reference.velocity = LimitVelocity(
        reference.velocity,
        std::hypot(input.target_velocity[0], input.target_velocity[1]));
  }
  const Vector error_position{reference.position[0] - input.pose.position[0],
                              reference.position[1] - input.pose.position[1],
                              0};
  auto desired = LimitVelocity(
      {reference.velocity[0] + config.gain * error_position[0],
       reference.velocity[1] + config.gain * error_position[1], 0},
      config.max_speed);
  if (config.follow_path) {
    desired = GovernSeparation(desired, input, config);
  }
  const auto yaw = std::atan2(east, north);
  const auto error = std::atan2(std::sin(yaw - input.previous_yaw),
                                std::cos(yaw - input.previous_yaw));
  const auto yaw_step = config.max_yaw_rate * input.dt;
  // Vision arrives slower than the control tick. Filter its bearing before
  // differentiating it; direct heading steps amplify delayed pixel noise into
  // alternating yaw-rate feed-forward and an unstable image-centering loop.
  const auto response =
      -std::expm1(-input.dt / config.heading_response_seconds);
  const auto step = std::clamp(error * response, -yaw_step, yaw_step);
  return {desired, input.previous_yaw + step, bearing,
          input.dt > 0 ? step / input.dt : 0};
}
}  // namespace raven::guidance
