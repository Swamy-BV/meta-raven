// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include "std_msgs/msg/header.hpp"
#include <rclcpp/parameter.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "../../core/result.h"
#include "adapters/ros/frame_input.h"
#include "application/engine.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/object_hypothesis.hpp"
#include "vision_msgs/msg/object_hypothesis_with_pose.hpp"
#include "vision_msgs/msg/vision_class.hpp"

namespace raven::vision {
namespace {
constexpr auto kPollMs = 50;
constexpr auto kDefaultAgeMs = 1000;
constexpr auto kMaxAgeMs = 10000;
constexpr auto kMicrosPerMillisecond = 1000;
constexpr auto kNanosPerSecond = 1000000000ULL;

std_msgs::msg::Header FrameHeader(const Result& result,
                                  const std::string& frame) {
  std_msgs::msg::Header header;
  header.stamp.sec =
      static_cast<std::int32_t>(result.stamp_ns / kNanosPerSecond);
  header.stamp.nanosec =
      static_cast<std::uint32_t>(result.stamp_ns % kNanosPerSecond);
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

VisionNode::VisionNode()
    : Node("vision_engine", "/raven/vehicle_1/perception") {
  Configure();
  CreatePublishers();
  CreateCommands();
  timer_ =
      create_wall_timer(std::chrono::milliseconds(kPollMs), [this] { Tick(); });
}

void VisionNode::Configure() {
  rcl_interfaces::msg::ParameterDescriptor options;
  options.read_only = true;
  const auto config = declare_parameter<std::string>(
      "config_file", "/etc/vision-engine.conf", options);
  const auto topic = declare_parameter<std::string>(
      "camera_topic", "/raven/vehicle_1/cameras/front/image_raw", options);
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
  frames_ = std::make_unique<RosFrameInput>(*this, topic);
  engine_ = std::make_unique<Engine>(
      config, [this] { return frames_->Latest(); },
      static_cast<std::uint64_t>(age) * kMicrosPerMillisecond);
  declare_parameter<std::vector<std::string>>("model_ids", engine_->Models(),
                                              options);
  const auto fallback =
      declare_parameter<std::string>("default_model", "", options);
  const auto selected =
      declare_parameter<std::string>("model", engine_->InitialModel(fallback));
  declare_parameter<bool>("auto_infer", true);
  if (!selected.empty()) {
    engine_->Select(selected);
  }
}

void VisionNode::CreatePublishers() {
  const auto metadata_qos = rclcpp::QoS(1).reliable().transient_local();
  const auto output_qos = rclcpp::QoS(5).reliable();
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
}

rcl_interfaces::msg::SetParametersResult VisionNode::UpdateParameters(
    const std::vector<rclcpp::Parameter>& parameters) {
  rcl_interfaces::msg::SetParametersResult reply;
  reply.successful = true;
  try {
    for (const auto& parameter : parameters) {
      ApplyParameter(parameter, parameters.size());
    }
  } catch (const std::exception& error) {
    reply.successful = false;
    reply.reason = error.what();
  }
  return reply;
}

void VisionNode::ApplyParameter(const rclcpp::Parameter& parameter,
                                std::size_t count) {
  if (parameter.get_name() == "use_sim_time" && parameter.as_bool()) {
    throw std::invalid_argument("simulation-time input is not supported");
  }
  if (parameter.get_name() != "model") {
    return;
  }
  if (count != 1) {
    throw std::invalid_argument("change model in a separate parameter request");
  }
  engine_->Select(parameter.as_string());
}

void VisionNode::CreateCommands() {
  parameters_ = add_on_set_parameters_callback(
      [this](const std::vector<rclcpp::Parameter>& values) {
        return UpdateParameters(values);
      });
  infer_ = create_service<std_srvs::srv::Trigger>(
      "infer",
      [this](const std_srvs::srv::Trigger::Request::ConstSharedPtr&,
             const std_srvs::srv::Trigger::Response::SharedPtr& reply) {
        try {
          reply->success = engine_->Infer();
          reply->message =
              reply->success
                  ? "Inference accepted; completion is published asynchronously"
                  : "Waiting for a newer camera frame";
        } catch (const std::exception& error) {
          reply->message = error.what();
        }
      });
  cancel_ = create_service<std_srvs::srv::Trigger>(
      "cancel",
      [this](const std_srvs::srv::Trigger::Request::ConstSharedPtr&,
             const std_srvs::srv::Trigger::Response::SharedPtr& reply) {
        const auto stopped =
            set_parameter(rclcpp::Parameter("auto_infer", false));
        if (!stopped.successful) {
          reply->message = stopped.reason;
          return;
        }
        engine_->Cancel();
        reply->success = true;
        reply->message =
            "Automatic inference paused and current job cancellation requested";
      });
}

void VisionNode::PublishMetadata(const Snapshot& snapshot) {
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

void VisionNode::PublishResult(const Result& result,
                               const std::vector<std::string>& labels) {
  const auto key =
      result.identity.instance + ':' + std::to_string(result.job_sequence);
  if (key == result_key_) {
    return;
  }
  const auto header = FrameHeader(result, result.optical_frame);
  if (result.task == "classification") {
    vision_msgs::msg::Classification message;
    message.header = header;
    for (const auto& value : result.classification) {
      message.results.push_back(HypothesisMessage(value, labels));
    }
    classification_->publish(message);
  } else {
    vision_msgs::msg::Detection2DArray message;
    message.header = header;
    for (const auto& value : result.detections) {
      message.detections.push_back(DetectionMessage(value, header, labels));
    }
    detections_->publish(message);
  }
  result_key_ = key;
}

void VisionNode::PublishHealth(const std::string& text, bool failed, bool fresh,
                               const Snapshot& snapshot) {
  diagnostic_msgs::msg::DiagnosticArray message;
  message.header.stamp = now();
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = "perception/pipeline";
  status.hardware_id = metadata_key_;
  status.level = failed  ? diagnostic_msgs::msg::DiagnosticStatus::ERROR
                 : fresh ? diagnostic_msgs::msg::DiagnosticStatus::OK
                         : diagnostic_msgs::msg::DiagnosticStatus::WARN;
  status.message = text;
  for (const auto& [key, value] : snapshot.metrics) {
    diagnostic_msgs::msg::KeyValue entry;
    entry.key = key;
    entry.value = value;
    status.values.push_back(entry);
  }
  message.status.push_back(status);
  health_->publish(message);
}

void VisionNode::Tick() {
  try {
    if (get_parameter("use_sim_time").as_bool()) {
      throw std::runtime_error(
          "source timestamp mapping does not support ROS simulation time");
    }
    const auto snapshot = engine_->Poll(get_parameter("auto_infer").as_bool());
    PublishMetadata(snapshot);
    if (snapshot.result) {
      PublishResult(*snapshot.result, snapshot.labels);
    }
    PublishHealth(snapshot.health, snapshot.failed, snapshot.result.has_value(),
                  snapshot);
  } catch (const std::exception& error) {
    PublishHealth(error.what(), true, false, {});
  }
}
}  // namespace raven::vision
