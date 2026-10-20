// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <optional>

namespace raven::following {
using Vector = std::array<double, 3>;
using Quaternion = std::array<double, 4>;

struct Pose {
  Vector position{};                   // Local NED, metres.
  Quaternion orientation{1, 0, 0, 0};  // Body FRD to NED, wxyz.
};

struct Camera {
  double fx = 0;
  double fy = 0;
  double cx = 0;
  double cy = 0;
  double pitch = 0;  // Fixed downward tilt from body forward, radians.
  Vector offset{};   // Camera origin in body FRD, metres.
};

struct Pixel {
  double x = 0;
  double y = 0;
};

inline constexpr double kDefaultDistance = 8;
inline constexpr double kDefaultGain = 0.4;
inline constexpr double kDefaultSpeed = 1.5;
inline constexpr double kDefaultAcceleration = 0.6;
inline constexpr double kDefaultYawRate = 0.4;
inline constexpr double kDefaultRange = 40;

struct Configuration {
  double distance = kDefaultDistance;
  double gain = kDefaultGain;
  double max_speed = kDefaultSpeed;
  double max_acceleration = kDefaultAcceleration;
  double max_yaw_rate = kDefaultYawRate;
  double ground_z = 0;
  double max_range = kDefaultRange;
};

struct FollowInput {
  Pose pose;
  Vector target{};
  Vector previous_velocity{};
  double previous_yaw = 0;
  double dt = 0;
};

struct Command {
  Vector velocity{};
  double yaw = 0;
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
}  // namespace raven::following
