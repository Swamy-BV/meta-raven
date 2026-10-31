// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include <Eigen/Geometry>
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>
#include <tf2_ros/transform_broadcaster.hpp>

#include "core/alignment.h"
#include "nav_msgs/msg/odometry.hpp"
#include "px4_msgs/msg/estimator_status_flags.hpp"
#include "px4_msgs/msg/vehicle_odometry.hpp"
#include "px4_msgs/msg/vehicle_status.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace raven::navigation {

inline constexpr std::size_t kHistoryCapacity = 200;
inline constexpr std::int64_t kPoseGapNs = 100000000;
inline constexpr std::int64_t kDefaultAgeNs = 200000000;
inline constexpr std::int64_t kNanosecondsPerSecond = 1000000000;
inline constexpr double kMaxTranslationError = 0.5;
inline constexpr double kMaxRotationError = 0.15;

// A single frame authority for an estimator session. No simulator truth input,
// arming client, or setpoint publisher is present in this component.
class FramesNode final : public rclcpp::Node {
 public:
  FramesNode();

 private:
  void Configure();
  void Connect();
  void ReceiveLocal(const nav_msgs::msg::Odometry& message);
  void ReceiveReference(const px4_msgs::msg::VehicleOdometry& message);
  void TryPublish();
  void PublishHealth();
  void Reset(const std_srvs::srv::Trigger::Response::SharedPtr& response);
  [[nodiscard]] bool Fresh(std::int64_t stamp_ns,
                           std::int64_t limit_ns = 0) const;

  PoseHistory references_{kHistoryCapacity, kPoseGapNs};
  FrameAlignment alignment_{kMaxTranslationError, kMaxRotationError};
  Eigen::Isometry3d imu_from_body_ = Eigen::Isometry3d::Identity();
  std::optional<TimedPose> local_;
  std::optional<std::uint8_t> reset_counter_;
  std::int64_t published_ns_ = 0;
  std::int64_t reference_ns_ = 0;
  std::int64_t fusion_ns_ = 0;
  std::int64_t status_ns_ = 0;
  std::int64_t max_age_ns_ = kDefaultAgeNs;
  bool fusion_valid_ = false;
  bool disarmed_ = false;
  std::string input_frame_, imu_frame_, local_frame_, reference_frame_;
  std::string body_frame_, local_topic_, reference_topic_, px4_namespace_;
  std::string reason_ = "Waiting for aligned VIO and PX4 acquisition times";
  std::unique_ptr<tf2_ros::TransformBroadcaster> broadcaster_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr local_input_;
  rclcpp::Subscription<px4_msgs::msg::VehicleOdometry>::SharedPtr
      reference_input_;
  rclcpp::Subscription<px4_msgs::msg::EstimatorStatusFlags>::SharedPtr
      fusion_input_;
  rclcpp::Subscription<px4_msgs::msg::VehicleStatus>::SharedPtr status_input_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr reset_;
  rclcpp::TimerBase::SharedPtr health_timer_;
};

}  // namespace raven::navigation
