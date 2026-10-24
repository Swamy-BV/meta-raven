// SPDX-License-Identifier: AGPL-3.0-only
#include "conversion.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <Eigen/Geometry>

#include "geometry_msgs/msg/vector3.hpp"

namespace raven::odometry {
namespace {
constexpr auto kNanosPerSecond = 1000000000LL;
constexpr auto kNanosPerMicrosecond = 1000LL;
constexpr double kQuaternionTolerance = 0.001;
constexpr double kCovarianceTolerance = 1e-8;
constexpr std::size_t kPoseDimensions = 6;
constexpr double kMaximumPosition = 1e6;
constexpr double kMaximumAngularRate = 50.0;
constexpr double kMaximumCovariance = 1e6;
using Covariance = Eigen::Matrix<double, kPoseDimensions, kPoseDimensions>;

Eigen::Matrix3d WorldRotation() {
  Eigen::Matrix3d rotation = Eigen::Matrix3d::Zero();
  rotation(0, 1) = 1.0;
  rotation(1, 0) = 1.0;
  rotation(2, 2) = -1.0;
  return rotation;
}

Eigen::Matrix3d BodyRotation() {
  Eigen::Matrix3d rotation = Eigen::Matrix3d::Identity();
  rotation(1, 1) = -1.0;
  rotation(2, 2) = -1.0;
  return rotation;
}

Covariance ReadCovariance(
    const std::array<double, kPoseDimensions * kPoseDimensions>& values) {
  Covariance matrix;
  for (std::size_t row = 0; row < kPoseDimensions; ++row) {
    for (std::size_t column = 0; column < kPoseDimensions; ++column) {
      matrix(static_cast<Eigen::Index>(row),
             static_cast<Eigen::Index>(column)) =
          values.at(row * kPoseDimensions + column);
    }
  }
  return matrix;
}

bool ValidCovariance(const Covariance& matrix) {
  return matrix.allFinite() &&
         matrix.cwiseAbs().maxCoeff() <= kMaximumCovariance &&
         matrix.isApprox(matrix.transpose(), kCovarianceTolerance) &&
         (matrix.diagonal().array() > 0.0).all() && matrix.ldlt().isPositive();
}

Eigen::Vector3d Vector(const geometry_msgs::msg::Vector3& value) {
  return {value.x, value.y, value.z};
}

std::string Validate(const nav_msgs::msg::Odometry& input,
                     const Settings& settings, std::int64_t now_ns) {
  if (input.header.frame_id != settings.world_frame ||
      input.child_frame_id != settings.body_frame) {
    return "unexpected world or body frame";
  }
  const auto sample_ns =
      static_cast<std::int64_t>(input.header.stamp.sec) * kNanosPerSecond +
      input.header.stamp.nanosec;
  if (sample_ns <= 0 || now_ns <= 0 ||
      input.header.stamp.nanosec >= kNanosPerSecond ||
      sample_ns - now_ns > settings.future_tolerance_ns ||
      now_ns - sample_ns > settings.max_age_ns) {
    return "invalid, future or stale acquisition timestamp";
  }
  const auto& pose = input.pose.pose;
  const Eigen::Vector3d position(pose.position.x, pose.position.y,
                                 pose.position.z);
  const Eigen::Quaterniond orientation(pose.orientation.w, pose.orientation.x,
                                       pose.orientation.y, pose.orientation.z);
  if (!position.allFinite() ||
      position.cwiseAbs().maxCoeff() > kMaximumPosition ||
      !orientation.coeffs().allFinite() ||
      std::abs(orientation.norm() - 1.0) > kQuaternionTolerance ||
      !Vector(input.twist.twist.linear).allFinite() ||
      !Vector(input.twist.twist.angular).allFinite() ||
      Vector(input.twist.twist.linear).norm() > settings.max_speed ||
      Vector(input.twist.twist.angular).norm() > kMaximumAngularRate) {
    return "invalid pose, quaternion or body velocity";
  }
  if (!ValidCovariance(ReadCovariance(input.pose.covariance)) ||
      !ValidCovariance(ReadCovariance(input.twist.covariance))) {
    return "missing or invalid covariance";
  }
  return {};
}

std::array<float, 3> Floats(const Eigen::Vector3d& value) {
  return {static_cast<float>(value.x()), static_cast<float>(value.y()),
          static_cast<float>(value.z())};
}

void SetVariances(px4_msgs::msg::VehicleOdometry& output,
                  const nav_msgs::msg::Odometry& input) {
  constexpr auto kStride = kPoseDimensions + 1;
  constexpr auto kRoll = 3;
  constexpr auto kPitch = 4;
  constexpr auto kYaw = 5;
  const auto& pose = input.pose.covariance;
  const auto& twist = input.twist.covariance;
  // Signed axis permutations only reorder diagonal variances. Body sign
  // changes preserve each diagonal; no off-diagonal term contributes.
  output.position_variance = {static_cast<float>(pose.at(kStride)),
                              static_cast<float>(pose.at(0)),
                              static_cast<float>(pose.at(2 * kStride))};
  output.orientation_variance = {static_cast<float>(pose.at(kRoll * kStride)),
                                 static_cast<float>(pose.at(kPitch * kStride)),
                                 static_cast<float>(pose.at(kYaw * kStride))};
  output.velocity_variance = {static_cast<float>(twist.at(0)),
                              static_cast<float>(twist.at(kStride)),
                              static_cast<float>(twist.at(2 * kStride))};
}
}  // namespace

Conversion Convert(const nav_msgs::msg::Odometry& input,
                   const Settings& settings, std::int64_t now_ns) {
  const auto error = Validate(input, settings, now_ns);
  if (!error.empty()) {
    return {std::nullopt, error};
  }
  const auto& pose = input.pose.pose;
  const auto world = WorldRotation();
  const auto body = BodyRotation();
  const Eigen::Quaterniond orientation(pose.orientation.w, pose.orientation.x,
                                       pose.orientation.y, pose.orientation.z);
  const Eigen::Quaterniond converted(world * orientation.toRotationMatrix() *
                                     body);
  px4_msgs::msg::VehicleOdometry output;
  output.timestamp = static_cast<std::uint64_t>(now_ns / kNanosPerMicrosecond);
  output.timestamp_sample = static_cast<std::uint64_t>(
      (static_cast<std::int64_t>(input.header.stamp.sec) * kNanosPerSecond +
       input.header.stamp.nanosec) /
      kNanosPerMicrosecond);
  output.pose_frame = px4_msgs::msg::VehicleOdometry::POSE_FRAME_FRD;
  output.position =
      Floats(world * Eigen::Vector3d(pose.position.x, pose.position.y,
                                     pose.position.z));
  output.q = {
      static_cast<float>(converted.w()), static_cast<float>(converted.x()),
      static_cast<float>(converted.y()), static_cast<float>(converted.z())};
  output.velocity_frame =
      px4_msgs::msg::VehicleOdometry::VELOCITY_FRAME_BODY_FRD;
  output.velocity = Floats(body * Vector(input.twist.twist.linear));
  output.angular_velocity = Floats(body * Vector(input.twist.twist.angular));
  SetVariances(output, input);
  // Availability only: this is not a calibrated percentage confidence score.
  output.quality = 1;
  return {output, {}};
}
}  // namespace raven::odometry
