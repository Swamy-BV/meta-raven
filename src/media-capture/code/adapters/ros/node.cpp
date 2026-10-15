// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <rmw/qos_profiles.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <image_transport/image_transport.hpp>
#include <rclcpp/parameter.hpp>
#include <rclcpp/utilities.hpp>

#include "../../media_config.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "settings.h"
#include <raven/contracts/camera.h>

namespace raven::media {
namespace {
constexpr std::uint64_t kMicrosPerSecond = 1000000;
constexpr std::uint32_t kNanosPerMicro = 1000;
constexpr std::uint32_t kRgbChannels = 3;
}  // namespace

MediaCaptureNode::MediaCaptureNode()
    : node_(std::make_shared<rclcpp::Node>("media_capture",
                                           "/raven/vehicle_1/cameras/front")) {
  rcl_interfaces::msg::ParameterDescriptor options;
  options.read_only = true;
  if (node_->get_parameter("use_sim_time").as_bool()) {
    throw std::invalid_argument("capture requires a system-time ROS clock");
  }
  optical_frame_ = node_->declare_parameter<std::string>(
      "optical_frame", "camera_optical_frame", options);
  if (optical_frame_.empty() || optical_frame_.front() == '/') {
    throw std::invalid_argument("optical_frame must be nonempty and relative");
  }
  auto qos = rmw_qos_profile_sensor_data;
  qos.depth = 1;
  publisher_ =
      image_transport::create_camera_publisher(node_.get(), "image_raw", qos);
  settings_ = ReadSettings(*node_);
  ConfigurePhoto();
  executor_.add_node(node_);
}

void MediaCaptureNode::ConfigurePhoto() {
  using Trigger = std_srvs::srv::Trigger;
  std::function<void(Trigger::Request::SharedPtr, Trigger::Response::SharedPtr)>
      callback = [this](const Trigger::Request::SharedPtr&,
                        const Trigger::Response::SharedPtr& reply) {
        CapturePhoto(*reply);
      };
  photo_ = node_->create_service<Trigger>("capture_photo", std::move(callback));
}

void MediaCaptureNode::CapturePhoto(std_srvs::srv::Trigger::Response& reply) {
  try {
    if (!snapshot_) {
      throw std::runtime_error("capture is not ready");
    }
    reply.message = snapshot_();
    reply.success = true;
  } catch (const std::exception& error) {
    reply.message = error.what();
    reply.success = false;
  }
}

const Config& MediaCaptureNode::Settings() const { return settings_; }

void MediaCaptureNode::BindSnapshot(std::function<std::string()> capture) {
  snapshot_ = std::move(capture);
}

void MediaCaptureNode::Spin() { executor_.spin_some(); }

bool MediaCaptureNode::Running() const { return rclcpp::ok(); }

bool MediaCaptureNode::WantsFrames() const {
  return publisher_.getNumSubscribers() != 0;
}

void MediaCaptureNode::Publish(raven::contracts::CameraFrame frame) {
  sensor_msgs::msg::Image image;
  image.header.frame_id = optical_frame_;
  // Network sources map stream time onto the receiver pipeline clock; this
  // timestamp must not be interpreted as a verified remote exposure time.
  image.header.stamp.sec =
      static_cast<std::int32_t>(frame.captured_unix_us / kMicrosPerSecond);
  image.header.stamp.nanosec =
      static_cast<std::uint32_t>(frame.captured_unix_us % kMicrosPerSecond) *
      kNanosPerMicro;
  image.width = frame.width;
  image.height = frame.height;
  image.encoding = "rgb8";
  image.is_bigendian = 0;
  image.step = frame.width * kRgbChannels;
  image.data = std::move(frame.data);
  sensor_msgs::msg::CameraInfo info;
  info.header = image.header;
  info.width = image.width;
  info.height = image.height;
  // K[0] == 0 explicitly denotes an uncalibrated camera. No invented
  // intrinsics or identity projection is supplied for replay/network sources.
  publisher_.publish(image, info);
}
}  // namespace raven::media
