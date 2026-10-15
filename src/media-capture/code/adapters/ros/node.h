// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <memory>
#include <string>

#include <image_transport/camera_publisher.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/service.hpp>

#include "../../media_config.h"
#include "../../ports/frame_output.h"
#include "std_srvs/srv/trigger.hpp"

namespace raven::media {
class MediaCaptureNode final : public FrameOutput {
 public:
  MediaCaptureNode();
  [[nodiscard]] const Config& Settings() const;
  void BindSnapshot(std::function<std::string()> capture) override;
  void Spin() override;
  [[nodiscard]] bool Running() const override;
  [[nodiscard]] bool WantsFrames() const override;
  void Publish(raven::contracts::CameraFrame frame) override;

 private:
  void ConfigurePhoto();
  void CapturePhoto(std_srvs::srv::Trigger::Response& reply);
  std::shared_ptr<rclcpp::Node> node_;
  image_transport::CameraPublisher publisher_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  std::string optical_frame_;
  Config settings_;
  std::function<std::string()> snapshot_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr photo_;
};
}  // namespace raven::media
