// SPDX-License-Identifier: AGPL-3.0-only

#include "settings.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <rclcpp/node.hpp>

#include "../../core/types.h"
#include "node.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"

namespace raven::media {
namespace {
template <class T>
T Parameter(rclcpp::Node& node, const std::string& name, const T& initial) {
  rcl_interfaces::msg::ParameterDescriptor descriptor;
  descriptor.read_only = true;
  return node.declare_parameter<T>(name, initial, descriptor);
}

int Checked(std::uint32_t value) {
  if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("profile value exceeds integer bounds");
  }
  return static_cast<int>(value);
}

void ReadProfile(rclcpp::Node& node, Profile& profile) {
  const auto prefix = profile.purpose + "_";
  profile.width = Parameter(node, prefix + "width", profile.width);
  profile.height = Parameter(node, prefix + "height", profile.height);
  profile.fps = Parameter(node, prefix + "fps", profile.fps);
  profile.bitrate_kbps =
      Parameter(node, prefix + "bitrate_kbps", profile.bitrate_kbps);
  profile.quality = Parameter(node, prefix + "quality", profile.quality);
}
}  // namespace

Profile FromMessage(const raven_interfaces::msg::MediaProfile& message) {
  return {message.purpose,         Checked(message.width),
          Checked(message.height), Checked(message.fps),
          message.codec,           Checked(message.bitrate_kbps),
          message.quality};
}

raven_interfaces::msg::MediaProfile ToMessage(const Profile& profile) {
  raven_interfaces::msg::MediaProfile message;
  message.purpose = profile.purpose;
  message.width = profile.width;
  message.height = profile.height;
  message.fps = profile.fps;
  message.codec = profile.codec;
  message.bitrate_kbps = profile.bitrate_kbps;
  message.quality = static_cast<std::uint8_t>(profile.quality);
  return message;
}

Settings MediaCaptureNode::ReadSettings() {
  Settings settings;
  settings.source.kind = Parameter(*this, "source", settings.source.kind);
  settings.source.uri = Parameter(*this, "input_uri", settings.source.uri);
  settings.source.camera_name =
      Parameter(*this, "camera_name", settings.source.camera_name);
  settings.source.image_topic =
      Parameter(*this, "input_image_topic", settings.source.image_topic);
  settings.source.camera_info_topic = Parameter(
      *this, "input_camera_info_topic", settings.source.camera_info_topic);
  settings.source.width =
      Parameter(*this, "source_width", settings.source.width);
  settings.source.height =
      Parameter(*this, "source_height", settings.source.height);
  settings.source.loop = Parameter(*this, "loop", settings.source.loop);
  settings.source.rtp_capture_timestamps = Parameter(
      *this, "rtp_capture_timestamps", settings.source.rtp_capture_timestamps);
  settings.converter = Parameter(*this, "converter", settings.converter);
  settings.output_dir = Parameter(*this, "output_dir", settings.output_dir);
  settings.stream_host = Parameter(*this, "stream_host", settings.stream_host);
  settings.stream_port = Parameter(*this, "stream_port", settings.stream_port);
  settings.recording = Parameter(*this, "recording", settings.recording);
  settings.streaming = Parameter(*this, "streaming", settings.streaming);
  settings.mode = Parameter(*this, "mode", settings.mode);
  settings.segment_seconds =
      Parameter(*this, "segment_seconds", settings.segment_seconds);
  ReadProfile(*this, settings.photo);
  ReadProfile(*this, settings.preview);
  ReadProfile(*this, settings.video);
  ReadProfile(*this, settings.image);
  Validate(settings);
  return settings;
}
}  // namespace raven::media
