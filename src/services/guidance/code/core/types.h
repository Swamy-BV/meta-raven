// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>  // IWYU pragma: export

namespace raven::guidance {
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
  double pitch = 0;  // Downward tilt in the mount frame, radians.
  Vector offset{};   // Camera origin in body FRD, metres.
  // Roll/pitch stabilization; yaw follows the aircraft.
  bool stabilized = false;
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
inline constexpr double kDefaultHeadingSpeed = 0.2;
inline constexpr double kDefaultRange = 40;
inline constexpr double kDefaultMaximumHeight = 12;

struct Configuration {
  double distance = kDefaultDistance;
  double gain = kDefaultGain;
  double max_speed = kDefaultSpeed;
  double max_acceleration = kDefaultAcceleration;
  double max_yaw_rate = kDefaultYawRate;
  double max_jerk = 1.0;
  double ground_z = 0;
  double minimum_height = 2;
  double maximum_height = kDefaultMaximumHeight;
  double max_range = kDefaultRange;
  bool follow_path = false;
  bool align_with_motion = false;
  double minimum_heading_speed = kDefaultHeadingSpeed;
  double motion_bearing_offset =
      0;  // Radians from target velocity toward its bearing.
};

struct Motion {
  Vector position{};
  Vector velocity{};
};

}  // namespace raven::guidance
