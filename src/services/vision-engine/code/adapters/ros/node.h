// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/node_interfaces/node_parameters_interface.hpp>
#include <rclcpp/parameter.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/timer.hpp>

#include "../../application/engine.h"
#include "../../core/result.h"
#include "frame_input.h"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "vision_msgs/msg/classification.hpp"
#include "vision_msgs/msg/detection2_d_array.hpp"
#include "vision_msgs/msg/label_info.hpp"
#include "vision_msgs/msg/vision_info.hpp"

namespace raven::vision {
class VisionNode final : public rclcpp::Node {
 public:
  VisionNode();

 private:
  void Configure();
  void CreatePublishers();
  void CreateCommands();
  rcl_interfaces::msg::SetParametersResult UpdateParameters(
      const std::vector<rclcpp::Parameter>& parameters);
  void ApplyParameter(const rclcpp::Parameter& parameter, std::size_t count);
  void Tick();
  void PublishMetadata(const Snapshot& snapshot);
  void PublishResult(const Result& result,
                     const std::vector<std::string>& labels);
  void TraceFrame(std::int64_t source_stamp_ns) const;
  void PublishHealth(const std::string& text, bool failed, bool fresh,
                     const Snapshot& snapshot);
  // Engine joins the job thread before the frame provider is destroyed.
  std::unique_ptr<RosFrameInput> frames_;
  std::unique_ptr<Engine> engine_;
  std::string frame_id_, metadata_key_, result_key_;
  bool metadata_initialized_ = false;
  rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr parameters_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr infer_, cancel_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<vision_msgs::msg::Classification>::SharedPtr
      classification_;
  rclcpp::Publisher<vision_msgs::msg::Detection2DArray>::SharedPtr detections_;
  rclcpp::Publisher<vision_msgs::msg::LabelInfo>::SharedPtr labels_;
  rclcpp::Publisher<vision_msgs::msg::VisionInfo>::SharedPtr info_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
};
}  // namespace raven::vision
