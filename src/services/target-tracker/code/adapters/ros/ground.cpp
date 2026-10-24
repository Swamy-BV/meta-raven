// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "std_msgs/msg/header.hpp"
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "core/ground_projector.h"
#include "core/tracker.h"
#include "node.h"
#include "px4_msgs/msg/vehicle_attitude.hpp"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "sensor_msgs/msg/camera_info.hpp"

namespace raven::tracking {
namespace {
constexpr auto kPoseQueueDepth = 16;
constexpr auto kMaximumTopicLength = 256U;
constexpr auto kPrincipalPointY = 5U;
constexpr auto kMaximumGroundClasses = 16U;
constexpr double kHalfTurnDegrees = 180;
constexpr double kPi = 3.14159265358979323846;
constexpr std::int64_t kNanosPerMicro = 1000;
constexpr std::int64_t kPoseTimeout = 750000000;
constexpr std::int64_t kFutureTolerance = 100000000;

std::optional<std::int64_t> PoseStamp(std::uint64_t stamp,
                                      const rclcpp::Time& clock) {
  if (stamp == 0 ||
      stamp > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max() / kNanosPerMicro)) {
    return std::nullopt;
  }
  const auto source = static_cast<std::int64_t>(stamp) * kNanosPerMicro;
  const auto age = clock.nanoseconds() - source;
  return age < -kFutureTolerance || age > kPoseTimeout
             ? std::nullopt
             : std::optional<std::int64_t>{source};
}

bool Rectified(const sensor_msgs::msg::CameraInfo& info) {
  constexpr std::array<double, 9> kIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1};
  constexpr double kTolerance = 1e-12;
  return std::all_of(info.d.begin(), info.d.end(),
                     [](double value) {
                       return std::isfinite(value) &&
                              std::abs(value) <= kTolerance;
                     }) &&
         std::equal(info.r.begin(), info.r.end(), kIdentity.begin(),
                    [](double left, double right) {
                      return std::isfinite(left) &&
                             std::abs(left - right) <= kTolerance;
                    });
}
}  // namespace

void TrackingNode::ConfigureGround() {
  rcl_interfaces::msg::ParameterDescriptor options;
  options.read_only = true;
  config_.use_ground_motion =
      declare_parameter<bool>("ground_association", false, options);
  if (!config_.use_ground_motion) {
    return;
  }
  if (!encoder_) {
    throw std::invalid_argument(
        "Ground recovery requires a vehicle ReID model");
  }
  ground_classes_ = declare_parameter<std::vector<std::string>>(
      "ground_classes", vehicle_classes_, options);
  if (ground_classes_.empty() ||
      ground_classes_.size() > kMaximumGroundClasses) {
    throw std::invalid_argument("Invalid ground-association classes");
  }
  GroundCamera camera;
  camera.ground_z = declare_parameter<double>("ground_z", 0, options);
  camera.pitch = declare_parameter<double>("camera_pitch_deg", 0, options) *
                 kPi / kHalfTurnDegrees;
  camera.stabilized =
      declare_parameter<bool>("camera_stabilized", false, options);
  camera.max_range = declare_parameter<double>("max_ground_range",
                                               kDefaultGroundRange, options);
  const auto offset = declare_parameter<std::vector<double>>(
      "camera_offset", {0, 0, 0}, options);
  if (offset.size() != camera.offset.size()) {
    throw std::invalid_argument("Camera offset requires body-FRD x/y/z metres");
  }
  std::copy(offset.begin(), offset.end(), camera.offset.begin());
  camera_info_topic_ = declare_parameter<std::string>(
      "camera_info_topic", "/raven/vehicle_1/cameras/front/camera_info",
      options);
  px4_namespace_ = declare_parameter<std::string>("px4_namespace", "", options);
  if (camera_info_topic_.empty() ||
      px4_namespace_.size() > kMaximumTopicLength ||
      (!px4_namespace_.empty() &&
       (px4_namespace_.front() != '/' || px4_namespace_.back() == '/'))) {
    throw std::invalid_argument("Invalid camera calibration or PX4 namespace");
  }
  ground_ = std::make_unique<GroundProjector>(camera);
}

void TrackingNode::ConnectGround() {
  if (!ground_) {
    return;
  }
  const auto qos = rclcpp::SensorDataQoS().keep_last(kPoseQueueDepth);
  positions_ = create_subscription<px4_msgs::msg::VehicleLocalPosition>(
      px4_namespace_ + "/fmu/out/vehicle_local_position_v1", qos,
      [this](
          const px4_msgs::msg::VehicleLocalPosition::ConstSharedPtr& message) {
        GroundPosition(*message);
      });
  attitudes_ = create_subscription<px4_msgs::msg::VehicleAttitude>(
      px4_namespace_ + "/fmu/out/vehicle_attitude", qos,
      [this](const px4_msgs::msg::VehicleAttitude::ConstSharedPtr& message) {
        GroundAttitude(*message);
      });
  calibration_ = create_subscription<sensor_msgs::msg::CameraInfo>(
      camera_info_topic_, qos,
      [this](const sensor_msgs::msg::CameraInfo::ConstSharedPtr& message) {
        GroundCalibrationInfo(*message);
      });
}

void TrackingNode::GroundPosition(
    const px4_msgs::msg::VehicleLocalPosition& message) {
  const auto stamp = PoseStamp(message.timestamp_sample, now());
  if (!stamp || !message.xy_valid || !message.z_valid) {
    ground_->Reset();
    return;
  }
  const std::array<std::uint8_t, 3> counters{message.xy_reset_counter,
                                             message.z_reset_counter,
                                             message.heading_reset_counter};
  if (position_resets_ && *position_resets_ != counters) {
    Reset("PX4 local-frame reset; ground tracking cleared");
  }
  position_resets_ = counters;
  ground_->Position(*stamp, {message.x, message.y, message.z});
}

void TrackingNode::GroundAttitude(
    const px4_msgs::msg::VehicleAttitude& message) {
  const auto stamp = PoseStamp(message.timestamp_sample, now());
  if (!stamp) {
    ground_->Reset();
    return;
  }
  if (attitude_reset_ && *attitude_reset_ != message.quat_reset_counter) {
    Reset("PX4 attitude reset; ground tracking cleared");
  }
  attitude_reset_ = message.quat_reset_counter;
  ground_->Attitude(*stamp,
                    {message.q[0], message.q[1], message.q[2], message.q[3]});
}

void TrackingNode::GroundCalibrationInfo(
    const sensor_msgs::msg::CameraInfo& message) {
  if (!Rectified(message)) {
    ground_->InvalidateCalibration();
    Reset("Ground tracking requires rectified camera calibration");
    return;
  }
  try {
    if (ground_->Calibrate({message.header.frame_id, message.k[0], message.k[4],
                            message.k[2], message.k[kPrincipalPointY],
                            message.width, message.height})) {
      Reset("Camera calibration changed; ground tracking cleared");
    }
  } catch (const std::invalid_argument&) {
    ground_->InvalidateCalibration();
    Reset("Invalid camera calibration; ground tracking cleared");
  }
}

void TrackingNode::Locate(std::vector<Observation>& observations,
                          const std_msgs::msg::Header& header) {
  if (!ground_) {
    return;
  }
  for (auto& observation : observations) {
    if (std::find(ground_classes_.begin(), ground_classes_.end(),
                  observation.class_id) == ground_classes_.end()) {
      continue;
    }
    observation.ground_point =
        ground_->Project(observation.box, header.frame_id,
                         rclcpp::Time(header.stamp).nanoseconds());
    if (observation.ground_point) {
      ++ground_observations_;
    } else {
      ++missing_ground_;
    }
  }
}
}  // namespace raven::tracking
