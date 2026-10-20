// SPDX-License-Identifier: AGPL-3.0-only
#include "follower.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

namespace raven::following {
namespace {
constexpr double kMinimumRayDown = 0.05;
constexpr double kMinimumRange = 0.2;
constexpr double kQuaternionTolerance = 0.01;

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
  const auto ray = Rotate(pose.orientation, body_ray);
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
  const auto range = std::max(std::hypot(north, east), kMinimumRange);
  const auto speed = std::clamp(config.gain * (range - config.distance),
                                -config.max_speed, config.max_speed);
  const Vector desired{speed * north / range, speed * east / range, 0};
  const auto delta_north = desired[0] - input.previous_velocity[0];
  const auto delta_east = desired[1] - input.previous_velocity[1];
  const auto delta = std::hypot(delta_north, delta_east);
  const auto step = config.max_acceleration * input.dt;
  const auto fraction = delta > step ? step / delta : 1.0;
  const auto yaw = std::atan2(east, north);
  const auto error = std::atan2(std::sin(yaw - input.previous_yaw),
                                std::cos(yaw - input.previous_yaw));
  const auto yaw_step = config.max_yaw_rate * input.dt;
  return {{input.previous_velocity[0] + fraction * delta_north,
           input.previous_velocity[1] + fraction * delta_east, 0},
          input.previous_yaw + std::clamp(error, -yaw_step, yaw_step)};
}
}  // namespace raven::following
