// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include "std_msgs/msg/header.hpp"
#include <rclcpp/logging.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "core/tracker.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/object_hypothesis_with_pose.hpp"

namespace raven::tracking {
namespace {
constexpr auto kWatchdogMs = 100;
constexpr double kDefaultInputTimeoutSeconds = 0.75;
constexpr double kMaxInputTimeoutSeconds = 10.0;
constexpr auto kOutputDepth = 5;
constexpr auto kNanosPerSecond = 1000000000LL;
constexpr auto kFutureToleranceNs = 100000000LL;
constexpr auto kMaxFrameLength = 256;
constexpr auto kMaxModelLength = 256;
constexpr auto kMaxLocationLength = 1024;

std::int64_t Stamp(const std_msgs::msg::Header& header) {
  if (header.stamp.sec < 0 || header.stamp.nanosec >= kNanosPerSecond) {
    throw std::invalid_argument("invalid source timestamp");
  }
  return static_cast<std::int64_t>(header.stamp.sec) * kNanosPerSecond +
         header.stamp.nanosec;
}

Observation ObservationMessage(const vision_msgs::msg::Detection2D& value) {
  if (value.results.empty() || value.bbox.center.theta != 0) {
    throw std::invalid_argument(
        "tracking requires axis-aligned classified boxes");
  }
  const auto best =
      std::max_element(value.results.begin(), value.results.end(),
                       [](const auto& left, const auto& right) {
                         return left.hypothesis.score < right.hypothesis.score;
                       });
  return {{value.bbox.center.position.x, value.bbox.center.position.y,
           value.bbox.size_x, value.bbox.size_y},
          best->hypothesis.class_id,
          best->hypothesis.score};
}

vision_msgs::msg::Detection2D TrackMessage(
    const Track& track, const std_msgs::msg::Header& header) {
  vision_msgs::msg::Detection2D result;
  result.header = header;
  result.id = track.id;
  result.bbox.center.position.x = track.observation.box.center_x;
  result.bbox.center.position.y = track.observation.box.center_y;
  result.bbox.size_x = track.observation.box.width;
  result.bbox.size_y = track.observation.box.height;
  vision_msgs::msg::ObjectHypothesisWithPose hypothesis;
  hypothesis.hypothesis.class_id = track.observation.class_id;
  hypothesis.hypothesis.score = track.observation.score;
  result.results.push_back(hypothesis);
  return result;
}
}  // namespace

TrackingNode::TrackingNode()
    : Node("target_tracker", "/raven/vehicle_1/perception") {
  Configure();
  Connect();
  ConnectImages();
}

void TrackingNode::Configure() {
  rcl_interfaces::msg::ParameterDescriptor options;
  options.read_only = true;
  detections_topic_ =
      declare_parameter<std::string>("detections_topic", "detections", options);
  metadata_topic_ =
      declare_parameter<std::string>("metadata_topic", "vision_info", options);
  input_timeout_seconds_ = declare_parameter<double>(
      "input_timeout_seconds", kDefaultInputTimeoutSeconds, options);
  if (!std::isfinite(input_timeout_seconds_) || input_timeout_seconds_ <= 0 ||
      input_timeout_seconds_ > kMaxInputTimeoutSeconds) {
    throw std::invalid_argument("invalid detection input timeout");
  }
  camera_compensation_ =
      declare_parameter<bool>("camera_motion_compensation", false, options);
  image_topic_ = declare_parameter<std::string>(
      "image_topic", "/raven/vehicle_1/cameras/front/image_raw", options);
  if (camera_compensation_ && image_topic_.empty()) {
    throw std::invalid_argument("Compensation image topic must be set");
  }
  config_.class_agnostic =
      declare_parameter<bool>("class_agnostic", false, options);
  config_.lost_seconds =
      declare_parameter<double>("lost_seconds", config_.lost_seconds, options);
  config_.low_threshold = declare_parameter<double>(
      "low_threshold", config_.low_threshold, options);
  config_.high_threshold = declare_parameter<double>(
      "high_threshold", config_.high_threshold, options);
  config_.new_threshold = declare_parameter<double>(
      "new_threshold", config_.new_threshold, options);
  config_.match_threshold = declare_parameter<double>(
      "match_threshold", config_.match_threshold, options);
  const auto tracks = declare_parameter<int>(
      "max_tracks_per_class", static_cast<int>(config_.max_tracks_per_class),
      options);
  const auto classes = declare_parameter<int>(
      "max_classes", static_cast<int>(config_.max_classes), options);
  const auto detections = declare_parameter<int>(
      "max_detections", static_cast<int>(config_.max_detections), options);
  if (tracks <= 0 || classes <= 0 || detections <= 0 ||
      detections_topic_.empty()) {
    throw std::invalid_argument("invalid tracking limits or input topic");
  }
  config_.max_tracks_per_class = static_cast<std::size_t>(tracks);
  config_.max_classes = static_cast<std::size_t>(classes);
  config_.max_detections = static_cast<std::size_t>(detections);
  tracker_ = std::make_unique<Tracker>(config_);
  metadata_ready_ = metadata_topic_.empty();
  model_detection_ = metadata_ready_;
}

void TrackingNode::Connect() {
  output_ = create_publisher<vision_msgs::msg::Detection2DArray>(
      "tracks", rclcpp::QoS(kOutputDepth).reliable());
  health_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "tracking_health", rclcpp::QoS(1).reliable().transient_local());
  input_ = create_subscription<vision_msgs::msg::Detection2DArray>(
      detections_topic_, rclcpp::QoS(1).best_effort(),
      [this](
          const vision_msgs::msg::Detection2DArray::ConstSharedPtr& message) {
        Receive(*message);
      });
  if (!metadata_topic_.empty()) {
    metadata_ = create_subscription<vision_msgs::msg::VisionInfo>(
        metadata_topic_, rclcpp::QoS(1).reliable().transient_local(),
        [this](const vision_msgs::msg::VisionInfo::ConstSharedPtr& message) {
          Metadata(*message);
        });
  }
  reset_ = create_service<std_srvs::srv::Trigger>(
      "reset_tracking",
      [this](const std_srvs::srv::Trigger::Request::ConstSharedPtr&,
             const std_srvs::srv::Trigger::Response::SharedPtr& response) {
        Reset("Tracking reset requested");
        response->success = true;
        response->message = status_;
      });
  timer_ = create_wall_timer(std::chrono::milliseconds(kWatchdogMs),
                             [this] { Tick(); });
}

void TrackingNode::Metadata(const vision_msgs::msg::VisionInfo& message) {
  try {
    const auto stamp = Stamp(message.header);
    if (stamp <= 0 || message.method.size() > kMaxModelLength ||
        message.database_location.size() > kMaxLocationLength) {
      throw std::invalid_argument("invalid model identity");
    }
    const auto key = message.method + ':' +
                     std::to_string(message.database_version) + ':' +
                     message.database_location + ':' + std::to_string(stamp);
    if (metadata_ready_ && key == model_key_) {
      return;
    }
    Reset("Model identity changed; waiting for new detections");
    metadata_stamp_ = stamp;
    model_key_ = key;
    metadata_ready_ = true;
    const std::string suffix = "/detection";
    model_detection_ =
        message.database_version > 0 &&
        message.method.size() >= suffix.size() &&
        message.method.compare(message.method.size() - suffix.size(),
                               suffix.size(), suffix) == 0;
  } catch (const std::exception& error) {
    metadata_ready_ = false;
    Reset(error.what());
  }
}

std::vector<Observation> TrackingNode::Observations(
    const vision_msgs::msg::Detection2DArray& message) const {
  if (message.detections.size() > config_.max_detections) {
    throw std::invalid_argument("too many detections");
  }
  std::vector<Observation> observations;
  observations.reserve(message.detections.size());
  for (const auto& detection : message.detections) {
    if (detection.header.frame_id != message.header.frame_id ||
        Stamp(detection.header) != Stamp(message.header) ||
        detection.results.size() > config_.max_detections) {
      throw std::invalid_argument(
          "detection header mismatch or too many hypotheses");
    }
    observations.push_back(ObservationMessage(detection));
  }
  return observations;
}

void TrackingNode::Receive(const vision_msgs::msg::Detection2DArray& message) {
  try {
    if (!metadata_ready_ || !model_detection_) {
      throw std::invalid_argument("waiting for an active detection model");
    }
    const auto stamp = Stamp(message.header);
    const auto age = now().nanoseconds() - stamp;
    if (message.header.frame_id.empty() ||
        message.header.frame_id.front() == '/' ||
        message.header.frame_id.size() > kMaxFrameLength ||
        stamp <= metadata_stamp_ || age < -kFutureToleranceNs ||
        age > static_cast<std::int64_t>(input_timeout_seconds_ *
                                        kNanosPerSecond)) {
      throw std::invalid_argument(
          "stale detection, invalid frame or incompatible clock");
    }
    if (last_stamp_ && stamp <= *last_stamp_) {
      throw std::invalid_argument("duplicate or out-of-order detection");
    }
    const auto observations = Observations(message);
    if (!frame_id_.empty() && frame_id_ != message.header.frame_id) {
      Reset("Camera optical frame changed");
    }
    if (camera_compensation_) {
      compensation_ =
          camera_motion_.Estimate(stamp, message.header.frame_id, observations);
    }
    const auto result =
        tracker_->Update(observations, stamp, compensation_.transform);
    frame_id_ = message.header.frame_id;
    last_stamp_ = stamp;
    last_header_ = message.header;
    last_receive_ = std::chrono::steady_clock::now();
    stale_ = false;
    failed_ = false;
    status_ = "Tracking fresh detections";
    Publish(result, message.header);
  } catch (const std::runtime_error& error) {
    Reset(error.what());
    failed_ = true;
  } catch (const std::exception& error) {
    status_ = error.what();
    failed_ = true;
  }
}

void TrackingNode::Publish(const std::vector<Track>& tracks,
                           const std_msgs::msg::Header& header) {
  vision_msgs::msg::Detection2DArray message;
  message.header = header;
  for (const auto& track : tracks) {
    message.detections.push_back(TrackMessage(track, header));
  }
  published_count_ = message.detections.size();
  TraceFrame(rclcpp::Time(header.stamp).nanoseconds());
  output_->publish(message);
}

void TrackingNode::Reset(const std::string& reason) {
  tracker_->Reset();
  camera_motion_.Reset();
  compensation_ = {};
  if (last_stamp_) {
    // Same acquisition header clears cached tracks; it is not a new
    // observation.
    Publish({}, last_header_);
  }
  last_stamp_.reset();
  frame_id_.clear();
  stale_ = true;
  failed_ = false;
  published_count_ = 0;
  status_ = reason;
}

void TrackingNode::Tick() {
  if (!stale_ && std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - last_receive_)
                         .count() > input_timeout_seconds_) {
    Reset("Detection input timed out; tracks cleared");
  }
  PublishHealth();
}

// This single ROS logging macro expands to many generated control statements.
// NOLINTNEXTLINE(readability-function-size)
void TrackingNode::TraceFrame(std::int64_t source_stamp_ns) const {
  RCLCPP_DEBUG(get_logger(),
               "timing stage=tracking source_ns=%" PRId64 " ros_ns=%" PRId64
               " steady_ns=%" PRId64,
               source_stamp_ns, now().nanoseconds(),
               static_cast<std::int64_t>(
                   std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count()));
}

void TrackingNode::PublishHealth() {
  diagnostic_msgs::msg::DiagnosticArray message;
  message.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = "perception/tracking";
  status.hardware_id = frame_id_;
  const bool uncompensated = camera_compensation_ && !compensation_.transform;
  status.level = failed_ ? diagnostic_msgs::msg::DiagnosticStatus::ERROR
                 : stale_ || uncompensated
                     ? diagnostic_msgs::msg::DiagnosticStatus::WARN
                     : diagnostic_msgs::msg::DiagnosticStatus::OK;
  status.message = status_;
  for (const auto& [key, value] :
       std::vector<std::pair<std::string, std::string>>{
           {"camera_motion", compensation_.status},
           {"camera_motion_inliers", std::to_string(compensation_.inliers)},
           {"camera_motion_ms", std::to_string(compensation_.milliseconds)},
           {"association",
            config_.class_agnostic ? "class-independent" : "per-class"},
           {"visible_tracks", std::to_string(published_count_)},
           {"input_timeout_seconds", std::to_string(input_timeout_seconds_)},
           {"lost_seconds", std::to_string(config_.lost_seconds)},
           {"retained_tracks", std::to_string(tracker_->RetainedCount())},
           {"model_identity", model_key_}}) {
    diagnostic_msgs::msg::KeyValue entry;
    entry.key = key;
    entry.value = value;
    status.values.push_back(entry);
  }
  message.status.push_back(status);
  health_->publish(message);
}
}  // namespace raven::tracking
