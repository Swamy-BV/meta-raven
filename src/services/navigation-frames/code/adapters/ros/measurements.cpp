// SPDX-License-Identifier: AGPL-3.0-only
#include <cmath>
#include <optional>
#include <string>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <rclcpp/time.hpp>

#include "core/alignment.h"
#include "nav_msgs/msg/odometry.hpp"
#include "node.h"
#include "px4_msgs/msg/vehicle_odometry.hpp"

namespace raven::navigation {
namespace {
constexpr double kQuaternionTolerance = 1e-3;
constexpr double kMaximumLocalPosition = 100000;

std::optional<Eigen::Isometry3d> PoseTransform(
    const Eigen::Vector3d& translation, const Eigen::Quaterniond& rotation) {
  if (!translation.allFinite() || translation.norm() > kMaximumLocalPosition ||
      !rotation.coeffs().allFinite() ||
      std::abs(rotation.norm() - 1) > kQuaternionTolerance) {
    return std::nullopt;
  }
  Eigen::Isometry3d pose = Eigen::Isometry3d::Identity();
  pose.translation() = translation;
  pose.linear() = rotation.normalized().toRotationMatrix();
  return pose;
}
}  // namespace

void FramesNode::ReceiveLocal(const nav_msgs::msg::Odometry& message) {
  if (message.header.frame_id != input_frame_ ||
      message.child_frame_id != imu_frame_) {
    alignment_.Invalidate();
    reason_ = "VIO coordinate frame changed or is not configured";
    return;
  }
  const auto stamp = rclcpp::Time(message.header.stamp).nanoseconds();
  if (!Fresh(stamp)) {
    return;
  }
  if (local_ && stamp <= local_->stamp_ns) {
    alignment_.Invalidate();
    reason_ = "VIO acquisition clock regressed or repeated";
    return;
  }
  const auto& pose = message.pose.pose;
  const auto transform = PoseTransform(
      Eigen::Vector3d(pose.position.x, pose.position.y, pose.position.z),
      Eigen::Quaterniond(pose.orientation.w, pose.orientation.x,
                         pose.orientation.y, pose.orientation.z));
  if (!transform) {
    return;
  }
  local_ = TimedPose{stamp, *transform * imu_from_body_};
  TryPublish();
}

void FramesNode::ReceiveReference(
    const px4_msgs::msg::VehicleOdometry& message) {
  const auto converted = AcquisitionNanoseconds(message.timestamp_sample);
  if (!converted ||
      message.pose_frame != px4_msgs::msg::VehicleOdometry::POSE_FRAME_NED) {
    return;
  }
  const auto stamp = *converted;
  if (!Fresh(stamp)) {
    return;
  }
  if (reset_counter_ && *reset_counter_ != message.reset_counter) {
    references_.Clear();
    alignment_.Invalidate();
    reason_ = "PX4 estimator reset changed the navigation origin";
    return;
  }
  const auto pose =
      PoseTransform(Eigen::Vector3d(message.position[0], message.position[1],
                                    message.position[2]),
                    Eigen::Quaterniond(message.q[0], message.q[1], message.q[2],
                                       message.q[3]));
  if (!pose || !references_.Push({stamp, NedFrdToEnuFlu(*pose)})) {
    return;
  }
  reference_ns_ = stamp;
  reset_counter_ = message.reset_counter;
  TryPublish();
}

}  // namespace raven::navigation
