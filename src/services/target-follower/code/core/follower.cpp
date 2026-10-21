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
constexpr double kMotionWindow = 0.6;
constexpr double kMinimumMotionSpan = 0.4;
constexpr double kMaximumPrediction = 0.35;
constexpr double kMaximumReacquisitionGap = 1.5;
constexpr double kMinimumVariance = 1e-6;
constexpr auto kMaximumObservations = 64U;
constexpr double kMinimumHeadingSpeed = 2.0;

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
  if (!config.align_with_motion || speed < kMinimumHeadingSpeed) {
    return std::atan2(east, north);
  }
  const auto desired =
      std::atan2(input.target_velocity[1], input.target_velocity[0]) +
      config.motion_bearing_offset;
  const auto error = std::atan2(std::sin(desired - input.previous_bearing),
                                std::cos(desired - input.previous_bearing));
  const auto step = config.max_yaw_rate * input.dt;
  return input.previous_bearing + std::clamp(error, -step, step);
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

void MotionEstimator::Reset() {
  observations_.clear();
  retained_velocity_.reset();
}

void MotionEstimator::Observe(const Vector& point, const double stamp) {
  if (!std::isfinite(stamp) ||
      !std::all_of(point.begin(), point.end(),
                   [](const double value) { return std::isfinite(value); })) {
    return;
  }
  if (!observations_.empty()) {
    const auto dt = stamp - observations_.back().stamp;
    if (dt <= 0) {
      return;
    }
    if (dt > kMaximumReacquisitionGap) {
      Reset();
    } else if (dt > kMaximumPrediction) {
      // Preserve velocity through a short occlusion, but fit only the new
      // segment. Estimates still require a fresh acquisition timestamp.
      observations_.clear();
    }
  }
  observations_.push_back({point, stamp});
  while (observations_.size() > kMaximumObservations ||
         stamp - observations_.front().stamp > kMotionWindow) {
    observations_.pop_front();
  }
  if (observations_.back().stamp - observations_.front().stamp >=
      kMinimumMotionSpan) {
    retained_velocity_ = Fit().velocity;
  }
}

std::optional<Motion> MotionEstimator::Estimate(const double stamp,
                                                const double max_speed) const {
  if (observations_.empty() || !std::isfinite(stamp) ||
      !std::isfinite(max_speed) || max_speed <= 0) {
    return std::nullopt;
  }
  const auto& latest = observations_.back();
  const auto age = stamp - latest.stamp;
  if (age < 0 || age > kMaximumPrediction) {
    return std::nullopt;
  }
  Motion motion{latest.point, retained_velocity_.value_or(Vector{})};
  if (latest.stamp - observations_.front().stamp >= kMinimumMotionSpan) {
    motion = Fit();
  }
  motion.velocity = LimitVelocity(motion.velocity, max_speed);
  for (std::size_t axis = 0; axis < motion.position.size(); ++axis) {
    motion.position[axis] += motion.velocity[axis] * age;
  }
  return motion;
}

Motion MotionEstimator::Fit() const {
  const auto& latest = observations_.back();
  Motion motion{latest.point, {0, 0, 0}};
  double mean_time = 0;
  Vector mean_point{};
  for (const auto& observation : observations_) {
    mean_time += observation.stamp - latest.stamp;
    for (std::size_t axis = 0; axis < mean_point.size(); ++axis) {
      mean_point[axis] += observation.point[axis];
    }
  }
  const auto count = static_cast<double>(observations_.size());
  mean_time /= count;
  for (auto& value : mean_point) {
    value /= count;
  }
  double variance = 0;
  Vector covariance{};
  for (const auto& observation : observations_) {
    const auto dt = observation.stamp - latest.stamp - mean_time;
    variance += dt * dt;
    for (std::size_t axis = 0; axis < covariance.size(); ++axis) {
      covariance[axis] += dt * (observation.point[axis] - mean_point[axis]);
    }
  }
  if (variance < kMinimumVariance) {
    return motion;
  }
  for (std::size_t axis = 0; axis < motion.velocity.size(); ++axis) {
    motion.velocity[axis] = covariance[axis] / variance;
  }
  for (std::size_t axis = 0; axis < motion.position.size(); ++axis) {
    motion.position[axis] =
        mean_point[axis] - motion.velocity[axis] * mean_time;
  }
  return motion;
}

Command Follow(const FollowInput& input, const Configuration& config) {
  const auto north = input.target[0] - input.pose.position[0];
  const auto east = input.target[1] - input.pose.position[1];
  const auto bearing = FormationBearing(input, config);
  auto reference = input.reference.value_or(Motion{
      {input.target[0] - config.distance * std::cos(bearing),
       input.target[1] - config.distance * std::sin(bearing), config.ground_z},
      input.target_velocity});
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
          input.previous_yaw + std::clamp(error, -yaw_step, yaw_step),
          bearing};
}
}  // namespace raven::following
