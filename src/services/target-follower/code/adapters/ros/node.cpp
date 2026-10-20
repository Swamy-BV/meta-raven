// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/object_hypothesis_with_pose.hpp"
#include "vision_msgs/msg/vision_class.hpp"

namespace raven::following {
namespace {
constexpr auto kPeriod = std::chrono::milliseconds(50);
constexpr double kDt = 0.05;
constexpr double kTelemetryTimeout = 0.5;
constexpr double kStatusTimeout = 2.0;
constexpr double kPoseToleranceUs = 100000;
constexpr auto kHistory = 120U;
constexpr auto kMaxDetections = 128U;
constexpr auto kMaxIdLength = 256U;
constexpr auto kNanoseconds = 1000000000LL;
constexpr auto kQueueDepth = 5;
constexpr auto kPrincipalPointY = 5U;
constexpr auto kNanosPerMicro = 1000;
constexpr double kDefaultTargetTimeout = 0.35;
constexpr double kDefaultLossTimeout = 1.5;
constexpr double kMaximumLossTimeout = 5;
constexpr auto kMaxLabels = 512U;
constexpr double kDefaultMaxDisplacement = 30;
constexpr double kFutureTolerance = 0.1;
constexpr double kMinimumHeight = 2;
constexpr double kMaximumHeight = 12;
constexpr double kMaximumPitch = 1.4;
constexpr double kMaximumOffset = 2;

bool Finite(const double value) { return std::isfinite(value); }

template <class Value>
std::optional<Value> Nearest(
    const std::deque<std::pair<std::uint64_t, Value>>& history,
    const std::uint64_t timestamp) {
  const auto distance = [timestamp](const auto& sample) {
    return std::abs(static_cast<double>(sample.first) -
                    static_cast<double>(timestamp));
  };
  const auto sample = std::min_element(
      history.begin(), history.end(), [&](const auto& left, const auto& right) {
        return distance(left) < distance(right);
      });
  if (sample == history.end() || distance(*sample) > kPoseToleranceUs) {
    return std::nullopt;
  }
  return sample->second;
}

template <class Value>
void Append(std::deque<std::pair<std::uint64_t, Value>>& history,
            const std::uint64_t timestamp, const Value& value) {
  if (!history.empty() && timestamp <= history.back().first) {
    return;
  }
  history.emplace_back(timestamp, value);
  while (history.size() > kHistory) {
    history.pop_front();
  }
}

bool ValidCalibration(const sensor_msgs::msg::CameraInfo& info) {
  constexpr auto kMaximumDimension = 4096U;
  return info.width > 0 && info.height > 0 && info.width <= kMaximumDimension &&
         info.height <= kMaximumDimension &&
         std::all_of(info.k.begin(), info.k.end(), Finite) && info.k[0] > 0 &&
         info.k[4] > 0 &&
         std::all_of(info.d.begin(), info.d.end(),
                     [](const double value) { return value == 0; }) &&
         !info.header.frame_id.empty();
}
}  // namespace

FollowingNode::FollowingNode()
    : Node("target_follower", "/raven/vehicle_1/autonomy") {
  Configure();
  Connect();
}

void FollowingNode::Configure() {
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
  config_.distance = number("follow_distance", config_.distance);
  config_.gain = number("position_gain", config_.gain);
  config_.max_speed = number("max_speed", config_.max_speed);
  config_.max_acceleration =
      number("max_acceleration", config_.max_acceleration);
  config_.max_yaw_rate = number("max_yaw_rate", config_.max_yaw_rate);
  config_.ground_z = number("ground_z", config_.ground_z);
  config_.max_range = number("max_target_range", config_.max_range);
  timeout_seconds_ = number("target_timeout", kDefaultTargetTimeout);
  max_displacement_ = number("max_displacement", kDefaultMaxDisplacement);
  camera_.pitch = number("camera_pitch", camera_.pitch);
  camera_.offset = {number("camera_forward", 0), number("camera_right", 0),
                    number("camera_down", 0)};
  if (px4_namespace_.empty() || px4_namespace_.front() != '/' ||
      config_.distance <= 0 || config_.gain <= 0 || config_.max_speed <= 0 ||
      config_.max_acceleration <= 0 || config_.max_yaw_rate <= 0 ||
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

void FollowingNode::Connect() {
  const auto sensor = rclcpp::SensorDataQoS().keep_last(kQueueDepth);
  mode_ = create_publisher<px4_msgs::msg::OffboardControlMode>(
      px4_namespace_ + "/fmu/in/offboard_control_mode", sensor);
  setpoint_ = create_publisher<px4_msgs::msg::TrajectorySetpoint>(
      px4_namespace_ + "/fmu/in/trajectory_setpoint", sensor);
  health_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "following_health", rclcpp::QoS(1).reliable().transient_local());
  input_ = create_subscription<vision_msgs::msg::Detection2DArray>(
      tracks_topic_, kQueueDepth,
      [this](const vision_msgs::msg::Detection2DArray& message) {
        ReceiveTracks(message);
      });
  calibration_ = create_subscription<sensor_msgs::msg::CameraInfo>(
      info_topic_, sensor, [this](const sensor_msgs::msg::CameraInfo& message) {
        ReceiveInfo(message);
      });
  metadata_ = create_subscription<vision_msgs::msg::VisionInfo>(
      metadata_topic_, rclcpp::QoS(1).reliable().transient_local(),
      [this](const vision_msgs::msg::VisionInfo& message) {
        ReceiveMetadata(message);
      });
  labels_ = create_subscription<vision_msgs::msg::LabelInfo>(
      labels_topic_, rclcpp::QoS(1).reliable().transient_local(),
      [this](const vision_msgs::msg::LabelInfo& message) {
        ReceiveLabels(message);
      });
  position_input_ = create_subscription<px4_msgs::msg::VehicleLocalPosition>(
      px4_namespace_ + "/fmu/out/vehicle_local_position_v1", sensor,
      [this](const px4_msgs::msg::VehicleLocalPosition& message) {
        ReceivePosition(message);
      });
  attitude_input_ = create_subscription<px4_msgs::msg::VehicleAttitude>(
      px4_namespace_ + "/fmu/out/vehicle_attitude", sensor,
      [this](const px4_msgs::msg::VehicleAttitude& message) {
        ReceiveAttitude(message);
      });
  status_input_ = create_subscription<px4_msgs::msg::VehicleStatus>(
      px4_namespace_ + "/fmu/out/vehicle_status_v4", sensor,
      [this](const px4_msgs::msg::VehicleStatus& message) {
        status_ = message;
        last_status_ = Clock::now();
      });
  select_ = create_service<Select>(
      "select_target", [this](const Select::Request::SharedPtr& request,
                              const Select::Response::SharedPtr& response) {
        SelectTarget(*request, *response);
      });
  enable_ = create_service<Enable>(
      "set_following", [this](const Enable::Request::SharedPtr& request,
                              const Enable::Response::SharedPtr& response) {
        EnableFollowing(*request, *response);
      });
  control_timer_ = create_wall_timer(kPeriod, [this] { Tick(); });
  health_timer_ =
      create_wall_timer(std::chrono::seconds(1), [this] { PublishHealth(); });
}

bool FollowingNode::FreshTracks() const {
  const auto age =
      static_cast<double>(now().nanoseconds() -
                          rclcpp::Time(tracks_.header.stamp).nanoseconds()) /
      kNanoseconds;
  return age >= -kFutureTolerance && age <= timeout_seconds_;
}

void FollowingNode::ReceiveInfo(const sensor_msgs::msg::CameraInfo& message) {
  if (!ValidCalibration(message)) {
    info_.reset();
    if (enabled_) {
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

bool FollowingNode::Supported(
    const vision_msgs::msg::Detection2D& detection) const {
  return model_stamp_ > 0 && model_stamp_ == label_stamp_ &&
         detection.results.size() == 1 &&
         supported_classes_.find(
             detection.results.front().hypothesis.class_id) !=
             supported_classes_.end();
}

void FollowingNode::ReceiveLabels(const vision_msgs::msg::LabelInfo& message) {
  const auto stamp = rclcpp::Time(message.header.stamp).nanoseconds();
  if (stamp < label_stamp_ || message.class_map.size() > kMaxLabels) {
    return;
  }
  if (enabled_ && stamp != model_stamp_) {
    Hold("Vision labels changed; select the target again");
    selected_id_.clear();
  }
  supported_classes_.clear();
  for (const auto& label : message.class_map) {
    if (label.class_name == "person" || label.class_name == "car") {
      supported_classes_.insert(std::to_string(label.class_id));
    }
  }
  label_stamp_ = stamp;
}

void FollowingNode::ReceiveMetadata(
    const vision_msgs::msg::VisionInfo& message) {
  const auto stamp = rclcpp::Time(message.header.stamp).nanoseconds();
  if (stamp < model_stamp_) {
    return;
  }
  const auto key = message.database_location + ":" +
                   std::to_string(message.database_version) + ":" +
                   std::to_string(stamp);
  if (!model_key_.empty() && model_key_ != key) {
    Hold("Vision model changed; select the target again");
    selected_id_.clear();
  }
  model_key_ = key;
  model_stamp_ = stamp;
}

void FollowingNode::ReceivePosition(
    const px4_msgs::msg::VehicleLocalPosition& message) {
  if (!message.xy_valid || !message.z_valid ||
      !Finite(static_cast<double>(message.x)) ||
      !Finite(static_cast<double>(message.y)) ||
      !Finite(static_cast<double>(message.z))) {
    position_.reset();
    return;
  }
  if (position_ &&
      (position_.value().xy_reset_counter != message.xy_reset_counter ||
       position_.value().z_reset_counter != message.z_reset_counter ||
       position_.value().heading_reset_counter !=
           message.heading_reset_counter)) {
    Hold("Local position origin reset; output stopped for PX4 failsafe");
    streaming_ = false;
    positions_.clear();
    selected_id_.clear();
  }
  if (!positions_.empty() &&
      message.timestamp_sample <= positions_.back().first) {
    return;
  }
  position_ = message;
  Append(positions_, message.timestamp_sample,
         Vector{message.x, message.y, message.z});
  last_position_ = Clock::now();
}

void FollowingNode::ReceiveAttitude(
    const px4_msgs::msg::VehicleAttitude& message) {
  const Quaternion orientation{message.q[0], message.q[1], message.q[2],
                               message.q[3]};
  if (!ValidPose({{0, 0, 0}, orientation})) {
    return;
  }
  if (!attitudes_.empty() &&
      message.timestamp_sample <= attitudes_.back().first) {
    return;
  }
  Append(attitudes_, message.timestamp_sample, orientation);
  last_attitude_ = Clock::now();
}

Pose FollowingNode::LatestPose() const {
  if (!position_ || attitudes_.empty()) {
    throw std::logic_error("Vehicle pose unavailable");
  }
  return {{position_.value().x, position_.value().y, position_.value().z},
          attitudes_.back().second};
}

bool FollowingNode::FreshTelemetry() const {
  const auto current = Clock::now();
  const auto fresh = [current](const Clock::time_point& received) {
    return std::chrono::duration<double>(current - received).count() <=
           kTelemetryTimeout;
  };
  return position_ && status_ && !attitudes_.empty() && fresh(last_position_) &&
         fresh(last_attitude_) &&
         std::chrono::duration<double>(current - last_status_).count() <=
             kStatusTimeout;
}

FollowingNode::Projection FollowingNode::TargetPoint(
    const vision_msgs::msg::Detection2D& detection) const {
  if (!info_ || info_->header.frame_id != tracks_.header.frame_id) {
    return {std::nullopt, "Camera calibration/frame unavailable"};
  }
  if (!Supported(detection) || detection.bbox.center.theta != 0 ||
      !Finite(detection.bbox.size_x) || !Finite(detection.bbox.size_y) ||
      detection.bbox.size_x <= 0 || detection.bbox.size_y <= 0) {
    return {std::nullopt, "Invalid selected-target box or class"};
  }
  const Pixel foot{
      detection.bbox.center.position.x,
      detection.bbox.center.position.y + detection.bbox.size_y / 2};
  if (!Finite(foot.x) || !Finite(foot.y) || foot.x < 0 || foot.y < 0 ||
      foot.x >= info_->width || foot.y >= info_->height) {
    return {std::nullopt, "Target footpoint outside the calibrated image"};
  }
  const auto timestamp = static_cast<std::uint64_t>(
      rclcpp::Time(tracks_.header.stamp).nanoseconds() / kNanosPerMicro);
  const auto position = Nearest(positions_, timestamp);
  const auto attitude = Nearest(attitudes_, timestamp);
  if (!position || !attitude) {
    return {std::nullopt,
            "No vehicle pose within 100 ms of the image timestamp"};
  }
  const auto point =
      ProjectGround(foot, camera_, {*position, *attitude}, config_);
  return {point,
          point ? "" : "Invalid pose, ground intersection or target range"};
}

void FollowingNode::ReceiveTracks(
    const vision_msgs::msg::Detection2DArray& message) {
  if (message.header.stamp.sec < 0 ||
      message.header.stamp.nanosec >= kNanoseconds ||
      message.detections.size() > kMaxDetections ||
      rclcpp::Time(message.header.stamp) <=
          rclcpp::Time(tracks_.header.stamp)) {
    return;
  }
  tracks_ = message;
  if (!enabled_ || !FreshTracks()) {
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
  const auto projection = TargetPoint(*found);
  if (!projection.point) {
    ++rejected_observations_;
    last_rejection_ = projection.reason;
    return;
  }
  target_ = projection.point;
  last_rejection_.clear();
  last_target_ = Clock::now();
  ++observations_used_;
}

void FollowingNode::SelectTarget(const Select::Request& request,
                                 Select::Response& response) {
  response.success = false;
  if (enabled_) {
    response.message = "Stop following before changing the target";
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
    response.message = "Person or car track is not visible";
    return;
  }
  selected_id_ = request.track_id;
  selected_frame_ = tracks_.header.frame_id;
  selected_class_ = found->results.front().hypothesis.class_id;
  state_ = "selected";
  reason_ = "Target selected; following is not enabled";
  response.success = true;
  response.message = reason_;
}

void FollowingNode::EnableFollowing(const Enable::Request& request,
                                    Enable::Response& response) {
  response.success = false;
  if (request.data && enabled_) {
    response.success = true;
    response.message = reason_;
    return;
  }
  if (!request.data) {
    Hold("Following stopped by request");
    selected_id_.clear();
    response.success = true;
    response.message = reason_;
    return;
  }
  if (!position_ || !status_ || !FreshTelemetry() || !FreshTracks() ||
      selected_id_.empty() || !info_ ||
      status_.value().arming_state !=
          px4_msgs::msg::VehicleStatus::ARMING_STATE_ARMED ||
      status_.value().nav_state !=
          px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_OFFBOARD ||
      status_.value().failsafe) {
    response.message =
        "Select a target with calibrated imagery and fresh telemetry; vehicle "
        "must already be armed in offboard";
    return;
  }
  const auto height =
      config_.ground_z - static_cast<double>(position_.value().z);
  if (height < kMinimumHeight || height > kMaximumHeight) {
    response.message =
        "Following altitude outside configured operating envelope";
    return;
  }
  const auto found = std::find_if(
      tracks_.detections.begin(), tracks_.detections.end(),
      [this](const auto& detection) { return detection.id == selected_id_; });
  if (found == tracks_.detections.end()) {
    response.message = "Selected target is no longer visible";
    return;
  }
  const auto projection = TargetPoint(*found);
  target_ = projection.point;
  if (!target_) {
    response.message = projection.reason;
    return;
  }
  origin_ = LatestPose().position;
  hold_position_ = origin_;
  command_ = {{0, 0, 0}, Heading(LatestPose().orientation)};
  last_target_ = Clock::now();
  enabled_ = true;
  streaming_ = true;
  state_ = "following";
  reason_ = "Following selected target on a level ground plane";
  response.success = true;
  response.message = reason_;
}

void FollowingNode::Hold(const std::string& reason) {
  if (enabled_ && position_) {
    hold_position_ = {position_.value().x, position_.value().y,
                      position_.value().z};
  }
  enabled_ = false;
  target_.reset();
  command_.velocity = {0, 0, 0};
  state_ = streaming_ ? "holding" : "idle";
  reason_ = reason;
}

void FollowingNode::Tick() {
  if (!streaming_) {
    return;
  }
  if (!position_ || !status_ || !FreshTelemetry()) {
    enabled_ = false;
    streaming_ = false;
    state_ = "unavailable";
    reason_ = "Telemetry stale; setpoints stopped for PX4 offboard failsafe";
    return;
  }
  if (status_.value().arming_state !=
          px4_msgs::msg::VehicleStatus::ARMING_STATE_ARMED ||
      status_.value().nav_state !=
          px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_OFFBOARD ||
      status_.value().failsafe) {
    Hold("PX4 left armed offboard control");
    streaming_ = false;
    return;
  }
  const auto target_age =
      std::chrono::duration<double>(Clock::now() - last_target_).count();
  if (enabled_ && target_age > loss_timeout_seconds_) {
    Hold(
        "Selected identity expired; holding until explicit selection and "
        "enable");
  } else if (enabled_ && target_age > timeout_seconds_) {
    WaitForTarget();
  } else if (enabled_) {
    state_ = "following";
    reason_ = "Following the same selected identity";
  }
  const auto pose = LatestPose();
  if (enabled_ &&
      std::hypot(pose.position[0] - origin_[0], pose.position[1] - origin_[1]) >
          max_displacement_) {
    Hold("Following displacement limit reached");
  }
  if (enabled_ && state_ == "following" && target_) {
    command_ =
        Follow({pose, *target_, command_.velocity, command_.yaw, kDt}, config_);
  }
  PublishCommand();
}

void FollowingNode::WaitForTarget() {
  if (state_ == "waiting") {
    return;
  }
  const auto pose = LatestPose();
  hold_position_[0] = pose.position[0];
  hold_position_[1] = pose.position[1];
  command_.velocity = {0, 0, 0};
  state_ = "waiting";
  reason_ = "Brief detection gap; holding for the same selected identity";
}

void FollowingNode::PublishCommand() {
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  px4_msgs::msg::OffboardControlMode mode;
  mode.timestamp =
      static_cast<std::uint64_t>(now().nanoseconds() / kNanosPerMicro);
  mode.position = true;
  mode_->publish(mode);
  px4_msgs::msg::TrajectorySetpoint setpoint;
  setpoint.timestamp = mode.timestamp;
  const auto moving = enabled_ && state_ == "following";
  setpoint.position =
      moving ? std::array<float, 3>{nan, nan,
                                    static_cast<float>(hold_position_[2])}
             : std::array<float, 3>{static_cast<float>(hold_position_[0]),
                                    static_cast<float>(hold_position_[1]),
                                    static_cast<float>(hold_position_[2])};
  setpoint.velocity =
      moving
          ? std::array<float, 3>{static_cast<float>(command_.velocity[0]),
                                 static_cast<float>(command_.velocity[1]), nan}
          : std::array<float, 3>{nan, nan, nan};
  setpoint.acceleration = {nan, nan, nan};
  setpoint.jerk = {nan, nan, nan};
  setpoint.yaw = static_cast<float>(command_.yaw);
  setpoint.yawspeed = nan;
  setpoint_->publish(setpoint);
  ++setpoints_sent_;
}

void FollowingNode::PublishHealth() {
  diagnostic_msgs::msg::DiagnosticArray report;
  report.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus health;
  health.name = "autonomy/target_following";
  health.level =
      state_ == "holding" || state_ == "waiting" || state_ == "unavailable"
          ? diagnostic_msgs::msg::DiagnosticStatus::WARN
          : diagnostic_msgs::msg::DiagnosticStatus::OK;
  health.message = reason_;
  const auto add = [&health](const std::string& key, const std::string& value) {
    diagnostic_msgs::msg::KeyValue metric;
    metric.key = key;
    metric.value = value;
    health.values.push_back(metric);
  };
  add("state", state_);
  add("selected_track", selected_id_);
  add("px4_namespace", px4_namespace_);
  add("setpoints_sent", std::to_string(setpoints_sent_));
  add("observations_used", std::to_string(observations_used_));
  add("rejected_observations", std::to_string(rejected_observations_));
  add("last_rejection", last_rejection_);
  if (target_ && position_) {
    const auto range = std::hypot(
        target_.value()[0] - static_cast<double>(position_.value().x),
        target_.value()[1] - static_cast<double>(position_.value().y));
    add("target_range_m", std::to_string(range));
  }
  add("velocity_north", std::to_string(command_.velocity[0]));
  add("velocity_east", std::to_string(command_.velocity[1]));
  add("projection",
      "box-footpoint to configured level ground; rectified pixels");
  report.status.push_back(health);
  health_->publish(report);
}
}  // namespace raven::following
