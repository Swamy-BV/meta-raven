// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/timer.hpp>

#include "../../core/result.h"
#include "../../ports/model_backend.h"
#include "raven_interfaces/msg/perception_result.hpp"
#include "vision_msgs/msg/classification.hpp"
#include "vision_msgs/msg/detection2_d_array.hpp"
#include "vision_msgs/msg/label_info.hpp"
#include "vision_msgs/msg/vision_info.hpp"

namespace raven::perception {
using BackendFactory = std::function<std::unique_ptr<ModelBackend>(
    const std::string&, const std::string&, bool, std::uint64_t)>;

class PerceptionNode final : public rclcpp::Node {
 public:
  explicit PerceptionNode(const BackendFactory& factory);

 private:
  void Tick();
  void PublishMetadata(const Snapshot& snapshot);
  void PublishResult(const Result& result,
                     const std::vector<std::string>& labels);
  void PublishHealth(const std::string& text, bool failed, bool fresh);
  std::unique_ptr<ModelBackend> backend_;
  std::string frame_id_, metadata_key_, result_key_;
  bool metadata_initialized_ = false;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<raven_interfaces::msg::PerceptionResult>::SharedPtr result_;
  rclcpp::Publisher<vision_msgs::msg::Classification>::SharedPtr
      classification_;
  rclcpp::Publisher<vision_msgs::msg::Detection2DArray>::SharedPtr detections_;
  rclcpp::Publisher<vision_msgs::msg::LabelInfo>::SharedPtr labels_;
  rclcpp::Publisher<vision_msgs::msg::VisionInfo>::SharedPtr info_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
};
}  // namespace raven::perception
