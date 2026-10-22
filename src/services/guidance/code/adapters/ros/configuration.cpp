// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "core/types.h"
#include "estimation/motion.h"
#include "node.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "util.h"

namespace raven::guidance {
void GuidanceNode::Configure() {
  rcl_interfaces::msg::ParameterDescriptor options;
  options.read_only = true;
  const auto number = [this, &options](const std::string& name,
                                       const double value) {
    const auto result = declare_parameter<double>(name, value, options);
    if (!Finite(result)) {
      throw std::invalid_argument("non-finite parameter: " + name);
    }
    return result;
  };
  px4_namespace_ =
      declare_parameter<std::string>("px4_namespace", "/simulation", options);
  tracks_topic_ = declare_parameter<std::string>(
      "tracks_topic", "/raven/vehicle_1/perception/tracks", options);
  info_topic_ = declare_parameter<std::string>(
      "camera_info_topic", "/raven/vehicle_1/cameras/front/camera_info",
      options);
  metadata_topic_ = declare_parameter<std::string>(
      "metadata_topic", "/raven/vehicle_1/perception/vision_info", options);
  labels_topic_ = declare_parameter<std::string>(
      "labels_topic", "/raven/vehicle_1/perception/label_info", options);
  loss_timeout_seconds_ = number("target_loss_timeout", kDefaultLossTimeout);
  allow_unmapped_motion_ =
      declare_parameter<bool>("allow_unmapped_motion", false, options);
  local_frame_ =
      declare_parameter<std::string>("local_frame", "px4_local_enu", options);
  local_ned_frame_ = declare_parameter<std::string>("local_ned_frame",
                                                    "px4_local_ned", options);
  config_.max_jerk = number("max_jerk", config_.max_jerk);
  config_.follow_path = declare_parameter<bool>("follow_path", false, options);
  config_.align_with_motion =
      declare_parameter<bool>("align_with_motion", false, options);
  config_.minimum_heading_speed =
      number("minimum_heading_speed", config_.minimum_heading_speed);
  config_.motion_bearing_offset =
      number("motion_bearing_offset", config_.motion_bearing_offset);
  config_.distance = number("follow_distance", config_.distance);
  config_.gain = number("position_gain", config_.gain);
  config_.max_speed = number("max_speed", config_.max_speed);
  config_.max_acceleration =
      number("max_acceleration", config_.max_acceleration);
  config_.max_yaw_rate = number("max_yaw_rate", config_.max_yaw_rate);
  config_.heading_response_seconds =
      number("heading_response_seconds", config_.heading_response_seconds);
  config_.ground_z = number("ground_z", config_.ground_z);
  config_.minimum_height = number("minimum_height", config_.minimum_height);
  config_.maximum_height = number("maximum_height", config_.maximum_height);
  config_.max_range = number("max_target_range", config_.max_range);
  timeout_seconds_ = number("target_timeout", kDefaultTargetTimeout);
  max_displacement_ = number("max_displacement", kDefaultMaxDisplacement);
  camera_.pitch = number("camera_pitch", camera_.pitch);
  camera_.stabilized =
      declare_parameter<bool>("camera_stabilized", false, options);
  camera_.offset = {number("camera_forward", 0), number("camera_right", 0),
                    number("camera_down", 0)};
  motion_ = MotionEstimator(
      number("target_position_stddev", kTargetPositionStddev),
      number("target_acceleration_noise", kTargetAccelerationNoise));
  ValidateConfiguration();
}

void GuidanceNode::ValidateConfiguration() const {
  if (config_.minimum_height < 0 ||
      config_.maximum_height <= config_.minimum_height ||
      local_frame_.empty() || local_ned_frame_.empty() ||
      local_frame_ == local_ned_frame_ || config_.max_jerk <= 0 ||
      (!px4_namespace_.empty() && px4_namespace_.front() != '/') ||
      config_.distance <= 0 || config_.gain <= 0 || config_.max_speed <= 0 ||
      config_.max_acceleration <= 0 || config_.max_yaw_rate <= 0 ||
      config_.heading_response_seconds <= 0 ||
      config_.max_range <= config_.distance || timeout_seconds_ <= 0 ||
      timeout_seconds_ > kTelemetryTimeout ||
      loss_timeout_seconds_ <= timeout_seconds_ ||
      loss_timeout_seconds_ > kMaximumLossTimeout || max_displacement_ <= 0 ||
      std::abs(camera_.pitch) > kMaximumPitch ||
      std::any_of(camera_.offset.begin(), camera_.offset.end(),
                  [](const double value) {
                    return std::abs(value) > kMaximumOffset;
                  })) {
    throw std::invalid_argument("invalid following configuration");
  }
}

}  // namespace raven::guidance
