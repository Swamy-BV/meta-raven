// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <deque>
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
inline constexpr double kDefaultRange = 40;

struct Configuration {
  double distance = kDefaultDistance;
  double gain = kDefaultGain;
  double max_speed = kDefaultSpeed;
  double max_acceleration = kDefaultAcceleration;
  double max_yaw_rate = kDefaultYawRate;
  double ground_z = 0;
  double max_range = kDefaultRange;
  bool follow_path = false;
  bool align_with_motion = false;
  double motion_bearing_offset =
      0;  // Radians from target velocity toward its bearing.
};

struct Motion {
  Vector position{};
  Vector velocity{};
};

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
};

// Fits local-NED observations at capture time. Prediction is bounded to the
// freshness budget; it does not bridge a lost identity or a stale source.
class MotionEstimator {
 public:
  void Reset();
  void Observe(const Vector& point, double stamp);
  [[nodiscard]] std::optional<Motion> Estimate(double stamp,
                                               double max_speed) const;

 private:
  struct Observation {
    Vector point{};
    double stamp = 0;
  };

  [[nodiscard]] Motion Fit() const;
  std::deque<Observation> observations_;
  std::optional<Vector> retained_velocity_;
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
