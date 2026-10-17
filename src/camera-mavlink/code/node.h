// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <mavlink/v2.0/common/mavlink.h>

#include <chrono>
#include <cstdint>
#include <deque>
// ROS action-client template comparisons need this in some translation units.
#include <functional>  // IWYU pragma: keep
#include <map>
#include <optional>
#include <string>

#include <rclcpp/client.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>
#include <rclcpp_action/client.hpp>
#include <rclcpp_action/client_goal_handle.hpp>
#include <rclcpp_action/client_goal_handle_impl.hpp>

#include "link.h"
#include "raven_interfaces/action/capture_photos.hpp"
#include "raven_interfaces/msg/media_capabilities.hpp"
#include "raven_interfaces/msg/media_capture_event.hpp"
#include "raven_interfaces/msg/media_profile.hpp"
#include "raven_interfaces/msg/media_state.hpp"
#include "raven_interfaces/srv/set_media_mode.hpp"
#include "raven_interfaces/srv/set_media_output.hpp"

namespace raven::camera {
using Capture = raven_interfaces::action::CapturePhotos;
using CaptureGoal = rclcpp_action::ClientGoalHandle<Capture>;
using MediaState = raven_interfaces::msg::MediaState;
using Capabilities = raven_interfaces::msg::MediaCapabilities;

struct Command {
  mavlink_command_long_t parameters{};
  std::uint8_t system = 0;
  std::uint8_t component = 0;
  std::uint64_t token = 0;
};

struct Acknowledgement {
  Command command;
  std::uint8_t result = MAV_RESULT_FAILED;
  std::chrono::steady_clock::time_point expires;
};

// ROS is the only camera-control authority. This adapter neither opens camera
// devices nor translates flight telemetry: mavlink-router owns that routing.
class CameraNode final : public rclcpp::Node {
 public:
  CameraNode();

 private:
  void Tick();
  void Heartbeat();
  void Receive(const mavlink_message_t& message);
  void Dispatch(const Command& command);
  [[nodiscard]] bool InformationRequest(const Command& command);
  void Finish(const Command& command, std::uint8_t result);
  void Acknowledge(const Command& command, std::uint8_t result);
  [[nodiscard]] bool Repeated(const Command& command);
  [[nodiscard]] bool Fresh() const;
  [[nodiscard]] std::uint32_t BootMilliseconds() const;
  void StartPhotos(const Command& command);
  void PhotoAccepted(const Command& command,
                     const CaptureGoal::SharedPtr& handle);
  void PhotoResult(const CaptureGoal::WrappedResult& result);
  void StopPhotos(const Command& command);
  void SetOutput(const Command& command, const std::string& output,
                 bool enabled);
  void SetMode(const Command& command);
  void RequestMessage(const Command& command);
  void SetInterval(const Command& command);
  [[nodiscard]] bool Publish(std::uint32_t message_id);
  void CameraInformation();
  void CameraSettings();
  void CaptureStatus();
  void StorageInformation();
  void StreamInformation();
  void StreamStatus();
  void Captured(const raven_interfaces::msg::MediaCaptureEvent& event);
  [[nodiscard]] raven_interfaces::msg::MediaProfile VideoProfile() const;

  std::uint8_t system_;
  std::uint8_t component_;
  Link link_;
  std::string camera_namespace_;
  std::string stream_uri_;
  MediaState state_;
  Capabilities capabilities_;
  bool have_state_ = false;
  std::chrono::steady_clock::time_point state_received_;
  std::chrono::steady_clock::time_point started_;
  std::chrono::steady_clock::time_point recording_started_;
  std::chrono::steady_clock::time_point deadline_;
  std::optional<Command> pending_;
  std::uint64_t next_token_ = 0;
  std::deque<Acknowledgement> recent_;
  std::map<std::uint32_t, std::chrono::microseconds> intervals_;
  std::map<std::uint32_t, std::chrono::steady_clock::time_point>
      next_publication_;
  float photo_interval_ = 0;
  rclcpp::Subscription<MediaState>::SharedPtr state_subscription_;
  rclcpp::Subscription<Capabilities>::SharedPtr capabilities_subscription_;
  rclcpp::Subscription<raven_interfaces::msg::MediaCaptureEvent>::SharedPtr
      events_;
  rclcpp::Client<raven_interfaces::srv::SetMediaMode>::SharedPtr mode_;
  rclcpp::Client<raven_interfaces::srv::SetMediaOutput>::SharedPtr output_;
  rclcpp_action::Client<Capture>::SharedPtr capture_;
  CaptureGoal::SharedPtr capture_goal_;
  rclcpp::TimerBase::SharedPtr poll_;
  rclcpp::TimerBase::SharedPtr heartbeat_;
};
}  // namespace raven::camera
