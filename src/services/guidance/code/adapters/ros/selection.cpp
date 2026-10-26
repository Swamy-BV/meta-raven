// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "rclcpp/time.hpp"

#include "behaviors/trail.h"
#include "core/types.h"
#include "estimation/motion.h"
#include "node.h"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "util.h"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/object_hypothesis_with_pose.hpp"

namespace raven::guidance {
void GuidanceNode::SelectTarget(const Select::Request& request,
                                Select::Response& response) {
  response.success = false;
  if (goal_reserved_ || HasAction()) {
    response.message = "Cancel the active action before changing the target";
    return;
  }
  if (request.track_id.empty() || request.track_id.size() > kMaxIdLength ||
      !FreshTracks() || model_key_.empty()) {
    response.message = "A fresh visible track and vision metadata are required";
    return;
  }
  const auto found = std::find_if(
      tracks_.detections.begin(), tracks_.detections.end(),
      [this, &request](const auto& detection) {
        return detection.id == request.track_id && Supported(detection);
      });
  if (found == tracks_.detections.end()) {
    response.message = "Supported target track is not visible";
    return;
  }
  if (enabled_) {
    SwitchTarget(*found, response);
    return;
  }
  RememberTarget(*found);
  state_ = "selected";
  reason_ = "Target selected; following is not enabled";
  response.success = true;
  response.message = reason_;
  PublishHealth();
}

void GuidanceNode::RememberTarget(
    const vision_msgs::msg::Detection2D& detection) {
  motion_.Reset();
  trail_.Reset();
  target_.reset();
  target_stamp_ns_ = 0;
  selected_since_ = Clock::now();
  selected_id_ = detection.id;
  selected_frame_ = tracks_.header.frame_id;
  selected_class_ = detection.results.front().hypothesis.class_id;
  last_rejection_.clear();
}

void GuidanceNode::SwitchTarget(const vision_msgs::msg::Detection2D& detection,
                                Select::Response& response) {
  if (!streaming_ || !position_ || !FreshTelemetry() || !ArmedOffboard() ||
      !allow_unmapped_motion_) {
    response.message =
        "Active following with fresh armed Offboard telemetry is required";
    return;
  }
  const auto age =
      std::chrono::duration<double>(Clock::now() - last_target_).count();
  if (age > loss_timeout_seconds_) {
    response.message =
        "Selected identity expired; stop, select and enable again";
    return;
  }
  const auto height =
      config_.ground_z - static_cast<double>(position_.value().z);
  if (height < config_.minimum_height || height > config_.maximum_height ||
      selected_frame_ != tracks_.header.frame_id) {
    response.message =
        "Target switch requires the same camera and a valid following altitude";
    return;
  }
  const auto projection = TargetPoint(detection, std::nullopt);
  if (!projection.point) {
    response.message = projection.reason;
    return;
  }
  if (detection.id == selected_id_) {
    response.success = true;
    response.message = "Already following the selected target";
    return;
  }
  // Validate before committing. Forget the old object's velocity and trail,
  // while retaining vehicle reference derivatives, yaw and the flight origin.
  RememberTarget(detection);
  ObserveFollowing(*projection.point,
                   rclcpp::Time(tracks_.header.stamp).nanoseconds());
  state_ = "following";
  reason_ = "Following explicitly selected replacement target";
  ++target_switches_;
  response.success = true;
  response.message = reason_;
  PublishHealth();
}
}  // namespace raven::guidance
