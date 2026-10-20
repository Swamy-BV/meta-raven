// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "std_msgs/msg/header.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>

#include "core/tracker.h"
#include "std_srvs/srv/trigger.hpp"
#include "vision_msgs/msg/detection2_d_array.hpp"
#include "vision_msgs/msg/vision_info.hpp"

namespace raven::tracking {
// Default mutually-exclusive callback group serializes subscriptions, reset and
// watchdog. Steady time supervises silence; source time drives motion
// prediction.
class TrackingNode final : public rclcpp::Node {
 public:
  TrackingNode();

 private:
  void Configure();
  void Connect();
  void Receive(const vision_msgs::msg::Detection2DArray& message);
  void Metadata(const vision_msgs::msg::VisionInfo& message);
  std::vector<Observation> Observations(
      const vision_msgs::msg::Detection2DArray& message) const;
  void Publish(const std::vector<Track>& tracks,
               const std_msgs::msg::Header& header);
  void Tick();
  void Reset(const std::string& reason);
  void PublishHealth();
  Configuration config_;
  double input_timeout_seconds_ = 0;
  std::unique_ptr<Tracker> tracker_;
  std::string detections_topic_, metadata_topic_, frame_id_, model_key_;
  bool metadata_ready_ = false;
  bool model_detection_ = false;
  bool stale_ = true;
  bool failed_ = false;
  std::int64_t metadata_stamp_ = 0;
  std::optional<std::int64_t> last_stamp_;
  std::chrono::steady_clock::time_point last_receive_;
  std_msgs::msg::Header last_header_;
  std::string status_ = "Waiting for detections";
  std::size_t published_count_ = 0;
  rclcpp::Subscription<vision_msgs::msg::Detection2DArray>::SharedPtr input_;
  rclcpp::Subscription<vision_msgs::msg::VisionInfo>::SharedPtr metadata_;
  rclcpp::Publisher<vision_msgs::msg::Detection2DArray>::SharedPtr output_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr reset_;
  rclcpp::TimerBase::SharedPtr timer_;
};
}  // namespace raven::tracking
