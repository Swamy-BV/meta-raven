// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>

#include "core/follower.h"
#include "px4_msgs/msg/offboard_control_mode.hpp"
#include "px4_msgs/msg/trajectory_setpoint.hpp"
#include "px4_msgs/msg/vehicle_attitude.hpp"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "px4_msgs/msg/vehicle_status.hpp"
#include "raven_interfaces/srv/select_target.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/detection2_d_array.hpp"
#include "vision_msgs/msg/label_info.hpp"
#include "vision_msgs/msg/vision_info.hpp"

namespace raven::following {
// Default mutually-exclusive callbacks serialize selection, telemetry and
// control. This node never arms a vehicle or changes its flight mode.
class FollowingNode final : public rclcpp::Node {
 public:
  FollowingNode();

 private:
  struct Projection {
    std::optional<Vector> point;
    std::string reason;
  };

  using Clock = std::chrono::steady_clock;
  using Select = raven_interfaces::srv::SelectTarget;
  using Enable = std_srvs::srv::SetBool;
  void Configure();
  void Connect();
  void ReceiveTracks(const vision_msgs::msg::Detection2DArray& message);
  void ReceiveInfo(const sensor_msgs::msg::CameraInfo& message);
  void ReceiveLabels(const vision_msgs::msg::LabelInfo& message);
  [[nodiscard]] bool Supported(
      const vision_msgs::msg::Detection2D& detection) const;
  void WaitForTarget();
  void ReceiveMetadata(const vision_msgs::msg::VisionInfo& message);
  void ReceivePosition(const px4_msgs::msg::VehicleLocalPosition& message);
  void ReceiveAttitude(const px4_msgs::msg::VehicleAttitude& message);
  void SelectTarget(const Select::Request& request, Select::Response& response);
  void EnableFollowing(const Enable::Request& request,
                       Enable::Response& response);
  [[nodiscard]] bool FreshTelemetry() const;
  [[nodiscard]] bool FreshTracks() const;
  [[nodiscard]] Pose LatestPose() const;
  [[nodiscard]] Projection TargetPoint(
      const vision_msgs::msg::Detection2D& detection) const;
  void Tick();
  void Hold(const std::string& reason);
  void PublishCommand();
  void PublishHealth();

  Configuration config_;
  Camera camera_;
  std::string px4_namespace_, tracks_topic_, info_topic_, metadata_topic_,
      labels_topic_;
  std::string selected_id_, selected_frame_, selected_class_, model_key_;
  std::string state_ = "idle";
  std::string reason_ = "Select a visible person or car";
  double timeout_seconds_ = 0;
  double loss_timeout_seconds_ = 0;
  std::int64_t model_stamp_ = 0;
  std::int64_t label_stamp_ = 0;
  std::set<std::string> supported_classes_;
  double max_displacement_ = 0;
  bool enabled_ = false;
  bool streaming_ = false;
  std::optional<Vector> target_;
  Vector hold_position_{};
  Vector origin_{};
  Command command_;
  std::optional<sensor_msgs::msg::CameraInfo> info_;
  std::optional<px4_msgs::msg::VehicleStatus> status_;
  std::optional<px4_msgs::msg::VehicleLocalPosition> position_;
  vision_msgs::msg::Detection2DArray tracks_;
  std::deque<std::pair<std::uint64_t, Vector>> positions_;
  std::deque<std::pair<std::uint64_t, Quaternion>> attitudes_;
  Clock::time_point last_position_, last_attitude_, last_status_, last_target_;
  std::uint64_t setpoints_sent_ = 0;
  std::uint64_t observations_used_ = 0;
  std::uint64_t rejected_observations_ = 0;
  std::string last_rejection_;
  rclcpp::Subscription<vision_msgs::msg::Detection2DArray>::SharedPtr input_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr calibration_;
  rclcpp::Subscription<vision_msgs::msg::VisionInfo>::SharedPtr metadata_;
  rclcpp::Subscription<vision_msgs::msg::LabelInfo>::SharedPtr labels_;
  rclcpp::Subscription<px4_msgs::msg::VehicleStatus>::SharedPtr status_input_;
  rclcpp::Subscription<px4_msgs::msg::VehicleLocalPosition>::SharedPtr
      position_input_;
  rclcpp::Subscription<px4_msgs::msg::VehicleAttitude>::SharedPtr
      attitude_input_;
  rclcpp::Publisher<px4_msgs::msg::OffboardControlMode>::SharedPtr mode_;
  rclcpp::Publisher<px4_msgs::msg::TrajectorySetpoint>::SharedPtr setpoint_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
  rclcpp::Service<Select>::SharedPtr select_;
  rclcpp::Service<Enable>::SharedPtr enable_;
  rclcpp::TimerBase::SharedPtr control_timer_, health_timer_;
};
}  // namespace raven::following
