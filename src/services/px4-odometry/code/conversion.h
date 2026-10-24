// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "nav_msgs/msg/odometry.hpp"
#include "px4_msgs/msg/vehicle_odometry.hpp"

namespace raven::odometry {

inline constexpr double kDefaultMaxSpeed = 10.0;
inline constexpr auto kDefaultMaxAgeNs = 150000000LL;
inline constexpr auto kDefaultFutureToleranceNs = 5000000LL;

struct Settings {
  std::string world_frame = "global";
  std::string body_frame = "imu";
  std::int64_t max_age_ns = kDefaultMaxAgeNs;
  std::int64_t future_tolerance_ns = kDefaultFutureToleranceNs;
  double max_speed = kDefaultMaxSpeed;
};

struct Conversion {
  std::optional<px4_msgs::msg::VehicleOdometry> measurement;
  std::string error;
};

// Input is a gravity-aligned z-up world with arbitrary yaw and an FLU body.
// Pose covariance uses world position/body attitude; twist uses body axes.
// Timestamps must already share the host/PX4 epoch; arrival time is not a
// sample.
[[nodiscard]] Conversion Convert(const nav_msgs::msg::Odometry& input,
                                 const Settings& settings, std::int64_t now_ns);

}  // namespace raven::odometry
