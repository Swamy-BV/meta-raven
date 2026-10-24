// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/publisher.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/time.hpp"
#include <rclcpp_action/server_goal_handle.hpp>

#include "behaviors/follow.h"
#include "behaviors/trail.h"
#include "core/types.h"
#include "estimation/motion.h"
#include "estimation/pose.h"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "node.h"
#include "raven_interfaces/action/orbit.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "util.h"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/detection2_d_array.hpp"
#include "vision_msgs/msg/label_info.hpp"
#include "vision_msgs/msg/object_hypothesis_with_pose.hpp"
#include "vision_msgs/msg/vision_class.hpp"
#include "vision_msgs/msg/vision_info.hpp"

namespace raven::guidance {
void GuidanceNode::ConnectTargetOutputs() {
  const auto qos = rclcpp::SensorDataQoS().keep_last(kQueueDepth);
  observation_output_ = create_publisher<geometry_msgs::msg::PointStamped>(
      "~/target_observation", qos);
  estimate_output_ = create_publisher<geometry_msgs::msg::PointStamped>(
      "~/target_position", qos);
  observation_health_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "~/observation_health", qos);
}

void GuidanceNode::PublishPoint(
    const Vector& point,
    const rclcpp::Publisher<geometry_msgs::msg::PointStamped>::SharedPtr&
        publisher) const {
  // Projected estimates preserve image capture time and the local NED frame.
  geometry_msgs::msg::PointStamped message;
  message.header.stamp = tracks_.header.stamp;
  message.header.frame_id = local_ned_frame_;
  message.point.x = point[0];
  message.point.y = point[1];
  message.point.z = point[2];
  publisher->publish(message);
}

bool GuidanceNode::FreshTarget() const {
  const auto age = static_cast<double>(now().nanoseconds() - target_stamp_ns_) /
                   kNanoseconds;
  return age >= -kFutureTolerance && age <= timeout_seconds_;
}

bool GuidanceNode::FreshTracks() const {
  const auto age =
      static_cast<double>(now().nanoseconds() -
                          rclcpp::Time(tracks_.header.stamp).nanoseconds()) /
      kNanoseconds;
  return age >= -kFutureTolerance && age <= timeout_seconds_;
}

void GuidanceNode::ReceiveInfo(const sensor_msgs::msg::CameraInfo& message) {
  if (!ValidCalibration(message)) {
    info_.reset();
    if (enabled_ || orbit_goal_) {
      Hold("Calibrated rectified camera data unavailable");
    }
    return;
  }
  info_ = message;
  camera_.fx = message.k[0];
  camera_.fy = message.k[4];
  camera_.cx = message.k[2];
  camera_.cy = message.k[kPrincipalPointY];
}

bool GuidanceNode::Supported(
    const vision_msgs::msg::Detection2D& detection) const {
  return model_stamp_ > 0 && model_stamp_ == label_stamp_ &&
         detection.results.size() == 1 &&
         supported_classes_.find(
             detection.results.front().hypothesis.class_id) !=
             supported_classes_.end();
}

void GuidanceNode::ReceiveLabels(const vision_msgs::msg::LabelInfo& message) {
  const auto stamp = rclcpp::Time(message.header.stamp).nanoseconds();
  if (stamp < label_stamp_ || message.class_map.size() > kMaxLabels) {
    return;
  }
  if ((enabled_ || orbit_goal_ || state_ == "selected") &&
      stamp != model_stamp_) {
    Hold("Vision labels changed; select the target again");
    selected_id_.clear();
  }
  supported_classes_.clear();
  for (const auto& label : message.class_map) {
    if (label.class_name == "person" || label.class_name == "bicycle" ||
        label.class_name == "car") {
      supported_classes_.insert(std::to_string(label.class_id));
    }
  }
  label_stamp_ = stamp;
}

void GuidanceNode::ReceiveMetadata(
    const vision_msgs::msg::VisionInfo& message) {
  const auto stamp = rclcpp::Time(message.header.stamp).nanoseconds();
  if (stamp < model_stamp_) {
    return;
  }
  const auto key = message.database_location + ":" +
                   std::to_string(message.database_version) + ":" +
                   std::to_string(stamp);
  if ((enabled_ || orbit_goal_ || state_ == "selected") &&
      !model_key_.empty() && model_key_ != key) {
    Hold("Vision model changed; select the target again");
    selected_id_.clear();
  }
  model_key_ = key;
  model_stamp_ = stamp;
}

GuidanceNode::Projection GuidanceNode::TargetPoint(
    const vision_msgs::msg::Detection2D& detection,
    const std::optional<double> center_height) {
  alignment_.reset();
  if (!info_ || info_->header.frame_id != tracks_.header.frame_id) {
    return {std::nullopt, "Camera calibration/frame unavailable"};
  }
  if (!Supported(detection) || detection.bbox.center.theta != 0 ||
      !Finite(detection.bbox.size_x) || !Finite(detection.bbox.size_y) ||
      detection.bbox.size_x <= 0 || detection.bbox.size_y <= 0) {
    return {std::nullopt, "Invalid selected-target box or class"};
  }
  const Pixel foot{detection.bbox.center.position.x,
                   detection.bbox.center.position.y +
                       (center_height ? 0 : detection.bbox.size_y / 2)};
  if (!Finite(foot.x) || !Finite(foot.y) || foot.x < 0 || foot.y < 0 ||
      foot.x >= info_->width || foot.y >= info_->height) {
    return {std::nullopt, "Target measurement outside the calibrated image"};
  }
  const auto timestamp = static_cast<std::uint64_t>(
      rclcpp::Time(tracks_.header.stamp).nanoseconds() / kNanosPerMicro);
  alignment_ = AlignPose(positions_, attitudes_, timestamp);
  if (!alignment_) {
    ++pose_alignment_failures_;
    return {std::nullopt,
            "Image timestamp lacks bracketing vehicle pose within 100 ms"};
  }
  auto plane = config_;
  if (center_height) {
    plane.ground_z -= *center_height;
  }
  const auto point = ProjectGround(foot, camera_, alignment_->pose, plane);
  return {point,
          point ? "" : "Invalid pose, ground intersection or target range"};
}

void GuidanceNode::ReceiveTracks(
    const vision_msgs::msg::Detection2DArray& message) {
  if (message.header.stamp.sec < 0 ||
      message.header.stamp.nanosec >= kNanoseconds ||
      message.detections.size() > kMaxDetections ||
      rclcpp::Time(message.header.stamp) <=
          rclcpp::Time(tracks_.header.stamp)) {
    return;
  }
  tracks_ = message;
  const bool active_orbit = orbit_goal_ && !action_stopping_;
  if ((!enabled_ && !active_orbit && state_ != "selected") || !FreshTracks()) {
    return;
  }
  if (message.header.frame_id != selected_frame_) {
    Hold("Camera source changed; select the target again");
    return;
  }
  const auto found = std::find_if(
      message.detections.begin(), message.detections.end(),
      [this](const auto& detection) { return detection.id == selected_id_; });
  if (found == message.detections.end()) {
    return;
  }
  if (!Supported(*found) ||
      found->results.front().hypothesis.class_id != selected_class_) {
    Hold("Selected target class changed");
    return;
  }
  const auto center_height =
      active_orbit
          ? std::optional<double>{orbit_goal_->get_goal()->target_center_height}
          : std::nullopt;
  const auto projection = TargetPoint(*found, center_height);
  if (!projection.point) {
    ++rejected_observations_;
    last_rejection_ = projection.reason;
    PublishObservationHealth(last_rejection_);
    return;
  }
  PublishPoint(*projection.point, observation_output_);
  const auto stamp = rclcpp::Time(message.header.stamp).nanoseconds();
  if (active_orbit) {
    ObserveOrbit(*projection.point, stamp);
    return;
  }
  ObserveFollowing(*projection.point, stamp);
}

void GuidanceNode::ObserveFollowing(const Vector& point,
                                    const std::int64_t stamp) {
  if (!motion_.Observe(point, static_cast<double>(stamp) / kNanoseconds)) {
    ++rejected_observations_;
    last_rejection_ = "Target position rejected by motion filter";
    PublishObservationHealth(last_rejection_);
    return;
  }
  target_ = point;
  target_stamp_ns_ = stamp;
  const auto motion = motion_.Estimate(
      static_cast<double>(target_stamp_ns_) / kNanoseconds, config_.max_speed);
  if (motion) {
    PublishPoint(motion->position, estimate_output_);
    trail_.Observe(*motion);
  }
  last_rejection_.clear();
  last_target_ = Clock::now();
  ++observations_used_;
  PublishObservationHealth("");
}

}  // namespace raven::guidance
