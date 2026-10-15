// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "std_msgs/msg/header.hpp"
#include <rclcpp/parameter.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "../../core/result.h"
#include "../../ports/model_backend.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/object_hypothesis.hpp"
#include "vision_msgs/msg/object_hypothesis_with_pose.hpp"
#include "vision_msgs/msg/vision_class.hpp"

namespace raven::perception {
namespace {
constexpr auto kPollMs = 500;
constexpr auto kDefaultAgeMs = 1000;
constexpr auto kMaxAgeMs = 10000;
constexpr auto kMicrosPerMillisecond = 1000;
constexpr auto kMicrosPerSecond = 1000000;
constexpr auto kNanosPerMicro = 1000;

std_msgs::msg::Header FrameHeader(const Result& result,
                                  const std::string& frame) {
  std_msgs::msg::Header header;
  header.stamp.sec =
      static_cast<std::int32_t>(result.stamp_us / kMicrosPerSecond);
  header.stamp.nanosec = static_cast<std::uint32_t>(
      (result.stamp_us % kMicrosPerSecond) * kNanosPerMicro);
  header.frame_id = frame;
  return header;
}

vision_msgs::msg::ObjectHypothesis HypothesisMessage(
    const Hypothesis& value, const std::vector<std::string>& labels) {
  if (value.class_id >= labels.size()) {
    throw std::runtime_error("result class is outside active label map");
  }
  vision_msgs::msg::ObjectHypothesis message;
  message.class_id = std::to_string(value.class_id);
  message.score = value.score;
  return message;
}

vision_msgs::msg::Detection2D DetectionMessage(
    const Detection& value, const std_msgs::msg::Header& header,
    const std::vector<std::string>& labels) {
  vision_msgs::msg::Detection2D message;
  message.header = header;
  message.bbox.center.position.x = value.center_x;
  message.bbox.center.position.y = value.center_y;
  message.bbox.size_x = value.width;
  message.bbox.size_y = value.height;
  vision_msgs::msg::ObjectHypothesisWithPose hypothesis;
  hypothesis.hypothesis = HypothesisMessage(value.hypothesis, labels);
  message.results.push_back(hypothesis);
  return message;
}
}  // namespace

PerceptionNode::PerceptionNode(const BackendFactory& factory)
    : Node("perception", "/raven/vehicle_1/perception") {
  rcl_interfaces::msg::ParameterDescriptor options;
  options.read_only = true;
  const auto socket = declare_parameter<std::string>(
      "runner_socket", "/run/model-runner/control.sock", options);
  const auto assets = declare_parameter<std::string>(
      "model_assets", "/usr/share/model-runner/models", options);
  const auto automatic = declare_parameter<bool>("auto_infer", false, options);
  const auto age = declare_parameter<std::int64_t>("max_result_age_ms",
                                                   kDefaultAgeMs, options);
  frame_id_ = declare_parameter<std::string>("frame_id", "camera_optical_frame",
                                             options);
  if (age <= 0 || age > kMaxAgeMs || frame_id_.empty() ||
      frame_id_.front() == '/') {
    throw std::invalid_argument("invalid result age or optical frame");
  }
  if (get_parameter("use_sim_time").as_bool()) {
    throw std::invalid_argument(
        "source wall timestamps require a system-time ROS clock");
  }
  backend_ = factory(socket, assets, automatic,
                     static_cast<std::uint64_t>(age) * kMicrosPerMillisecond);
  const auto metadata_qos = rclcpp::QoS(1).reliable().transient_local();
  const auto output_qos = rclcpp::QoS(5).reliable();
  result_ = create_publisher<raven_interfaces::msg::PerceptionResult>(
      "result", output_qos);
  classification_ = create_publisher<vision_msgs::msg::Classification>(
      "classification", output_qos);
  detections_ = create_publisher<vision_msgs::msg::Detection2DArray>(
      "detections", output_qos);
  labels_ =
      create_publisher<vision_msgs::msg::LabelInfo>("label_info", metadata_qos);
  info_ = create_publisher<vision_msgs::msg::VisionInfo>("vision_info",
                                                         metadata_qos);
  health_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "health", metadata_qos);
  timer_ =
      create_wall_timer(std::chrono::milliseconds(kPollMs), [this] { Tick(); });
}

void PerceptionNode::PublishMetadata(const Snapshot& snapshot) {
  const auto key = snapshot.identity
                       ? snapshot.identity->instance + ':' +
                             std::to_string(snapshot.identity->generation)
                       : std::string{};
  if (metadata_initialized_ && key == metadata_key_) {
    return;
  }
  vision_msgs::msg::VisionInfo info;
  vision_msgs::msg::LabelInfo labels;
  info.header.stamp = now();
  info.header.frame_id = frame_id_;
  labels.header = info.header;
  if (snapshot.identity) {
    const auto& identity = *snapshot.identity;
    if (identity.generation >
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
      throw std::runtime_error(
          "model generation exceeds VisionInfo version range");
    }
    info.method = identity.model + '/' + identity.version + '/' + snapshot.task;
    info.database_location =
        std::string("ros-topic:") + labels_->get_topic_name();
    info.database_version = static_cast<std::int32_t>(identity.generation);
    for (std::size_t i = 0; i < snapshot.labels.size(); ++i) {
      vision_msgs::msg::VisionClass entry;
      entry.class_id = static_cast<std::uint16_t>(i);
      entry.class_name = snapshot.labels[i];
      labels.class_map.push_back(entry);
    }
    labels.threshold = snapshot.threshold;
  }
  labels_->publish(labels);
  info_->publish(info);
  metadata_key_ = key;
  metadata_initialized_ = true;
  result_key_.clear();
}

void PerceptionNode::PublishResult(const Result& result,
                                   const std::vector<std::string>& labels) {
  const auto key =
      result.identity.instance + ':' + std::to_string(result.job_sequence);
  if (key == result_key_) {
    return;
  }
  raven_interfaces::msg::PerceptionResult message;
  message.header = FrameHeader(result, result.optical_frame);
  message.instance_id = result.identity.instance;
  message.model_generation = result.identity.generation;
  message.job_sequence = result.job_sequence;
  message.model_id = result.identity.model;
  message.model_version = result.identity.version;
  message.model_sha256 = result.identity.sha;
  message.frame_sequence = result.frame_sequence;
  message.source_pts_ns = result.pts_ns;
  message.source_pts_valid = result.pts_valid;
  message.timestamp_basis =
      result.source_time_mapped ? "pipeline_clock" : "buffer_receipt";
  message.image_width = result.width;
  message.image_height = result.height;
  message.task = result.task;
  message.classification.header = message.header;
  message.detections.header = message.header;
  for (const auto& value : result.classification) {
    message.classification.results.push_back(HypothesisMessage(value, labels));
  }
  for (const auto& value : result.detections) {
    message.detections.detections.push_back(
        DetectionMessage(value, message.header, labels));
  }
  result_->publish(message);
  if (result.task == "classification") {
    classification_->publish(message.classification);
  } else {
    detections_->publish(message.detections);
  }
  result_key_ = key;
}

void PerceptionNode::PublishHealth(const std::string& text, bool failed,
                                   bool fresh) {
  diagnostic_msgs::msg::DiagnosticArray message;
  message.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = "perception/pipeline";
  status.hardware_id = metadata_key_;
  status.level = failed  ? diagnostic_msgs::msg::DiagnosticStatus::ERROR
                 : fresh ? diagnostic_msgs::msg::DiagnosticStatus::OK
                         : diagnostic_msgs::msg::DiagnosticStatus::WARN;
  status.message = text;
  message.status.push_back(status);
  health_->publish(message);
}

void PerceptionNode::Tick() {
  try {
    if (get_parameter("use_sim_time").as_bool()) {
      throw std::runtime_error(
          "source timestamp mapping does not support ROS simulation time");
    }
    const auto snapshot = backend_->Poll();
    PublishMetadata(snapshot);
    if (snapshot.result) {
      PublishResult(*snapshot.result, snapshot.labels);
    }
    PublishHealth(snapshot.health, snapshot.failed,
                  snapshot.result.has_value());
  } catch (const std::exception& error) {
    PublishHealth(error.what(), true, false);
  }
}
}  // namespace raven::perception
