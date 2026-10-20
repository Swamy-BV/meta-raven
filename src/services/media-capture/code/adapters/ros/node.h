// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "std_msgs/msg/header.hpp"
#include <camera_info_manager/camera_info_manager.hpp>
#include <image_transport/camera_publisher.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>
#include <rclcpp_action/server.hpp>
#include <rclcpp_action/server_goal_handle.hpp>

#include "../../application/session.h"
#include "../../core/types.h"
#include "../../ports/capture_backend.h"
#include "../../ports/media_store.h"
#include "raven_interfaces/action/capture_photos.hpp"
#include "raven_interfaces/msg/media_capabilities.hpp"
#include "raven_interfaces/msg/media_capture_event.hpp"
#include "raven_interfaces/msg/media_state.hpp"
#include "raven_interfaces/srv/set_camera_controls.hpp"
#include "raven_interfaces/srv/set_media_mode.hpp"
#include "raven_interfaces/srv/set_media_output.hpp"
#include "raven_interfaces/srv/set_media_profile.hpp"
#include "raven_interfaces/srv/set_media_source.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/image.hpp"
#include <raven/contracts/camera.h>

namespace raven::media {
using StoreFactory =
    std::function<std::unique_ptr<MediaStore>(const Settings&)>;

class MediaCaptureNode final : public rclcpp::Node {
 public:
  MediaCaptureNode(std::unique_ptr<CaptureBackend> backend,
                   const StoreFactory& store);
  void Stop();

 private:
  using Capture = raven_interfaces::action::CapturePhotos;
  using Goal = rclcpp_action::ServerGoalHandle<Capture>;
  void ConfigureServices();
  void ConfigureSourceServices();
  void ConfigureAction();
  void ConfigureInput();
  void InputFrame(const sensor_msgs::msg::Image& image);
  void ReceiveImage(const sensor_msgs::msg::Image& image);
  void ChangeSource(
      const raven_interfaces::srv::SetMediaSource::Request& request);
  void PublishNextFrame();
  void PublishFrame(raven::contracts::CameraFrame frame);
  void PublishState();
  void PublishCapabilities();
  void PublishPhoto(const Photo& photo);
  void TraceFrame(std::int64_t source_stamp_ns) const;
  [[nodiscard]] bool MapNativeStamp(std_msgs::msg::Header& header) const;
  void Tick();
  [[nodiscard]] Settings ReadSettings();
  std::unique_ptr<CaptureBackend> backend_;
  std::unique_ptr<MediaStore> store_;
  std::unique_ptr<Session> session_;
  image_transport::CameraPublisher images_;
  rclcpp::Publisher<raven_interfaces::msg::MediaState>::SharedPtr state_;
  rclcpp::Publisher<raven_interfaces::msg::MediaCapabilities>::SharedPtr
      capabilities_;
  rclcpp::Publisher<raven_interfaces::msg::MediaCaptureEvent>::SharedPtr
      events_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr input_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr input_info_;
  sensor_msgs::msg::CameraInfo calibration_;
  std::unique_ptr<camera_info_manager::CameraInfoManager> camera_info_;
  bool configured_calibration_valid_ = true;
  rclcpp::Service<raven_interfaces::srv::SetMediaProfile>::SharedPtr profile_;
  rclcpp::Service<raven_interfaces::srv::SetMediaSource>::SharedPtr source_;
  rclcpp::Service<raven_interfaces::srv::SetMediaMode>::SharedPtr mode_;
  rclcpp::Service<raven_interfaces::srv::SetMediaOutput>::SharedPtr output_;
  rclcpp::Service<raven_interfaces::srv::SetCameraControls>::SharedPtr
      controls_;
  rclcpp_action::Server<Capture>::SharedPtr capture_;
  std::shared_ptr<Goal> goal_;
  rclcpp::TimerBase::SharedPtr poll_;
  rclcpp::TimerBase::SharedPtr heartbeat_;
  std::string optical_frame_;
};
}  // namespace raven::media
