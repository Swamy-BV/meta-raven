// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include <Eigen/Core>  // IWYU pragma: keep -- Eigen transform expressions instantiate Core traits.
#include <Eigen/Geometry>
#include <rclcpp/create_publisher.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "core/alignment.h"
#include "geometry_msgs/msg/transform_stamped.hpp"

namespace raven::navigation {
namespace {
constexpr auto kSensorQueueDepth = 10;
constexpr auto kHealthPeriod = std::chrono::milliseconds(200);

geometry_msgs::msg::TransformStamped TransformMessage(
    const Eigen::Isometry3d& pose, const std::int64_t stamp_ns,
    const std::string& parent, const std::string& child) {
  geometry_msgs::msg::TransformStamped message;
  message.header.stamp = rclcpp::Time(stamp_ns);
  message.header.frame_id = parent;
  message.child_frame_id = child;
  message.transform.translation.x = pose.translation().x();
  message.transform.translation.y = pose.translation().y();
  message.transform.translation.z = pose.translation().z();
  const Eigen::Quaterniond rotation(pose.linear());
  message.transform.rotation.w = rotation.w();
  message.transform.rotation.x = rotation.x();
  message.transform.rotation.y = rotation.y();
  message.transform.rotation.z = rotation.z();
  return message;
}
}  // namespace

FramesNode::FramesNode() : Node("navigation_frames", "/localization") {
  Configure();
  Connect();
}

void FramesNode::Connect() {
  const auto qos = rclcpp::SensorDataQoS().keep_last(kSensorQueueDepth);
  broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);
  local_input_ = create_subscription<nav_msgs::msg::Odometry>(
      local_topic_, qos, [this](const nav_msgs::msg::Odometry& message) {
        ReceiveLocal(message);
      });
  reference_input_ = create_subscription<px4_msgs::msg::VehicleOdometry>(
      reference_topic_, qos,
      [this](const px4_msgs::msg::VehicleOdometry& message) {
        ReceiveReference(message);
      });
  fusion_input_ = create_subscription<px4_msgs::msg::EstimatorStatusFlags>(
      px4_namespace_ + "/fmu/out/estimator_status_flags", qos,
      [this](const px4_msgs::msg::EstimatorStatusFlags& message) {
        fusion_ns_ = AcquisitionNanoseconds(message.timestamp).value_or(0);
        fusion_valid_ = message.cs_ev_pos;
        TryPublish();
      });
  status_input_ = create_subscription<px4_msgs::msg::VehicleStatus>(
      px4_namespace_ + "/fmu/out/vehicle_status_v4", qos,
      [this](const px4_msgs::msg::VehicleStatus& message) {
        status_ns_ = AcquisitionNanoseconds(message.timestamp).value_or(0);
        disarmed_ = message.arming_state ==
                    px4_msgs::msg::VehicleStatus::ARMING_STATE_DISARMED;
      });
  health_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "~/health", rclcpp::QoS(1).reliable().transient_local());
  reset_ = create_service<std_srvs::srv::Trigger>(
      "~/reset",
      [this](const std_srvs::srv::Trigger::Request::SharedPtr&,
             const std_srvs::srv::Trigger::Response::SharedPtr& response) {
        Reset(response);
      });
  health_timer_ = create_wall_timer(kHealthPeriod, [this] { PublishHealth(); });
}

bool FramesNode::Fresh(const std::int64_t stamp_ns,
                       const std::int64_t limit_ns) const {
  constexpr auto kFutureToleranceNs = 5000000LL;
  return FreshAcquisition(stamp_ns, now().nanoseconds(),
                          limit_ns > 0 ? limit_ns : max_age_ns_,
                          kFutureToleranceNs);
}

void FramesNode::TryPublish() {
  if (alignment_.Faulted()) {
    reason_ = "Frame session invalidated; disarm and explicitly reset";
    return;
  }
  if (!local_ || !reset_counter_ || !Fresh(local_->stamp_ns) ||
      !Fresh(reference_ns_) || !Fresh(fusion_ns_, kNanosecondsPerSecond) ||
      !fusion_valid_) {
    reason_ = "Fresh VIO, PX4 odometry and external-vision fusion required";
    return;
  }
  const auto reference = references_.At(local_->stamp_ns);
  if (!reference) {
    reason_ = "PX4 acquisition-time bracket unavailable";
    return;
  }
  if (!alignment_.Observe(*reference, *local_, *reset_counter_)) {
    reason_ = "Estimator reset or excessive localization disagreement";
    return;
  }
  if (published_ns_ >= local_->stamp_ns) {
    return;
  }
  const auto aligned = alignment_.Transform();
  if (!aligned) {
    reason_ = "Navigation alignment unavailable";
    return;
  }
  broadcaster_->sendTransform(std::vector<geometry_msgs::msg::TransformStamped>{
      TransformMessage(*aligned, local_->stamp_ns, reference_frame_,
                       local_frame_),
      // Mapping and Guidance must use the same fused aircraft pose. The VIO
      // alignment is a separate branch, not a second authority for body TF.
      TransformMessage(reference->world_from_body, local_->stamp_ns,
                       reference_frame_, body_frame_)});
  published_ns_ = local_->stamp_ns;
  reason_ = "Acquisition-time frame chain available";
}

void FramesNode::PublishHealth() {
  diagnostic_msgs::msg::DiagnosticArray message;
  message.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = "navigation_frames";
  status.hardware_id = "localization";
  const bool valid = !alignment_.Faulted() && Fresh(published_ns_) &&
                     Fresh(reference_ns_) &&
                     Fresh(fusion_ns_, kNanosecondsPerSecond) && fusion_valid_;
  status.level = valid ? diagnostic_msgs::msg::DiagnosticStatus::OK
                       : diagnostic_msgs::msg::DiagnosticStatus::ERROR;
  status.message =
      valid ? reason_
            : "Navigation frame chain unavailable or stale: " + reason_;
  message.status.push_back(status);
  health_->publish(message);
}

void FramesNode::Reset(
    const std_srvs::srv::Trigger::Response::SharedPtr& response) {
  response->success = disarmed_ && Fresh(status_ns_, 3 * kNanosecondsPerSecond);
  if (!response->success) {
    response->message =
        "Fresh disarmed PX4 state required to reset map coordinates";
    return;
  }
  references_.Clear();
  local_.reset();
  alignment_.Reset();
  reset_counter_.reset();
  published_ns_ = 0;
  reference_ns_ = 0;
  reason_ =
      "New frame session; downstream maps must be cleared before enabling "
      "motion";
  response->message = reason_;
}

}  // namespace raven::navigation
