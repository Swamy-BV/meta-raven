// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
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

#include "adapters/neutron/vehicle_encoder.h"
#include "core/ground_projector.h"
#include "core/tracker.h"
#include "px4_msgs/msg/vehicle_attitude.hpp"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/image.hpp"
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
  void ConfigureGround();
  void ConnectGround();
  void GroundPosition(const px4_msgs::msg::VehicleLocalPosition& message);
  void GroundAttitude(const px4_msgs::msg::VehicleAttitude& message);
  void GroundCalibrationInfo(const sensor_msgs::msg::CameraInfo& message);
  void Locate(std::vector<Observation>& observations,
              const std_msgs::msg::Header& header);
  std::unique_ptr<GroundProjector> ground_;
  std::string camera_info_topic_, px4_namespace_;
  std::optional<std::array<std::uint8_t, 3>> position_resets_;
  std::optional<std::uint8_t> attitude_reset_;
  std::size_t ground_observations_ = 0, missing_ground_ = 0;
  rclcpp::Subscription<px4_msgs::msg::VehicleLocalPosition>::SharedPtr
      positions_;
  rclcpp::Subscription<px4_msgs::msg::VehicleAttitude>::SharedPtr attitudes_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr calibration_;

  void Frame(const sensor_msgs::msg::Image::ConstSharedPtr& message);
  void Encode(std::vector<Observation>& observations,
              const std_msgs::msg::Header& header);
  std::unique_ptr<VehicleEncoder> encoder_;
  std::deque<sensor_msgs::msg::Image::ConstSharedPtr> frames_;
  std::string image_topic_, reid_model_;
  std::vector<std::string> vehicle_classes_;
  std::size_t encoded_crops_ = 0;
  std::size_t missing_frames_ = 0;
  double reid_milliseconds_ = 0;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr images_;
  void Receive(const vision_msgs::msg::Detection2DArray& message);
  void Metadata(const vision_msgs::msg::VisionInfo& message);
  std::vector<Observation> Observations(
      const vision_msgs::msg::Detection2DArray& message) const;
  void Publish(const std::vector<Track>& tracks,
               const std_msgs::msg::Header& header);
  void Tick();
  void Reset(const std::string& reason);
  void TraceFrame(std::int64_t source_stamp_ns) const;
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
