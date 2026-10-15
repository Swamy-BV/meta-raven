// SPDX-License-Identifier: AGPL-3.0-only
#include "settings.h"

#include <filesystem>
#include <stdexcept>
#include <string>

#include <gio/gio.h>
#include <glib-object.h>

#include "../../media_config.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"

namespace {
constexpr auto kMinDimension = 64;
constexpr auto kMaxWidth = 1920;
constexpr auto kMaxHeight = 1080;
constexpr auto kMaxRate = 60;
constexpr auto kMinPort = 1024;
constexpr auto kMaxPort = 65535;
constexpr auto kMaxSegments = 120;
constexpr auto kMaxPhotos = 10000;
constexpr auto kMaxSegmentSeconds = 300;
constexpr auto kMinBitrate = 64;
constexpr auto kMaxBitrate = 10000;

template <class T>
T Parameter(rclcpp::Node& node, const std::string& name, const T& value) {
  rcl_interfaces::msg::ParameterDescriptor descriptor;
  descriptor.read_only = true;
  return node.declare_parameter<T>(name, value, descriptor);
}

void NumericAddress(const std::string& value) {
  GInetAddress* address = g_inet_address_new_from_string(value.c_str());
  if (!address) {
    throw std::invalid_argument("stream addresses must be numeric IPs");
  }
  g_object_unref(address);
}

void Validate(const Config& c) {
  if (c.source != "test" && c.source != "file" && c.source != "camera" &&
      c.source != "libcamera" && c.source != "rtp") {
    throw std::invalid_argument("unsupported capture source");
  }
  if ((c.codec != "h264" && c.codec != "h264-copy" && c.codec != "vp8") ||
      (c.converter != "g2d" && c.converter != "software") ||
      (c.codec == "h264-copy" && c.source != "file" && c.source != "rtp") ||
      (c.source == "rtp" && c.codec == "vp8")) {
    throw std::invalid_argument("unsupported source/codec/converter");
  }
  if (c.width < kMinDimension || c.width > kMaxWidth ||
      c.height < kMinDimension || c.height > kMaxHeight || c.width % 2 ||
      c.height % 2 || c.fps < 1 || c.fps > kMaxRate || c.port < kMinPort ||
      c.port > kMaxPort || c.rtp_port < kMinPort || c.rtp_port > kMaxPort ||
      (c.stream && c.source == "rtp" && c.port == c.rtp_port) ||
      c.segment < 2 || c.segment > kMaxSegmentSeconds || c.segments < 2 ||
      c.segments > kMaxSegments || c.snapshots < 1 ||
      c.snapshots > kMaxPhotos || c.bitrate < kMinBitrate ||
      c.bitrate > kMaxBitrate) {
    throw std::invalid_argument("capture parameters outside supported bounds");
  }
  NumericAddress(c.host);
  NumericAddress(c.rtp_address);
  if (c.output.empty() ||
      (c.source == "file" && !std::filesystem::is_regular_file(c.input))) {
    throw std::invalid_argument("invalid capture input or storage directory");
  }
}
}  // namespace

Config ReadSettings(rclcpp::Node& node) {
  Config c;
  c.source = Parameter(node, "source", c.source);
  c.input = Parameter(node, "input_file", c.input);
  c.device = Parameter(node, "device", c.device);
  c.camera_name = Parameter(node, "camera_name", c.camera_name);
  c.codec = Parameter(node, "codec", c.codec);
  c.converter = Parameter(node, "converter", c.converter);
  c.width = Parameter(node, "width", c.width);
  c.height = Parameter(node, "height", c.height);
  c.fps = Parameter(node, "fps", c.fps);
  c.bitrate = Parameter(node, "bitrate_kbps", c.bitrate);
  c.host = Parameter(node, "stream_host", c.host);
  c.port = Parameter(node, "stream_port", c.port);
  c.stream = Parameter(node, "stream", c.stream);
  c.rtp_address = Parameter(node, "rtp_address", c.rtp_address);
  c.rtp_port = Parameter(node, "rtp_port", c.rtp_port);
  c.output = Parameter(node, "output_dir", c.output);
  c.recording = Parameter(node, "recording", c.recording);
  c.segment = Parameter(node, "segment_seconds", c.segment);
  c.segments = Parameter(node, "max_segments", c.segments);
  c.snapshots = Parameter(node, "max_snapshots", c.snapshots);
  Validate(c);
  return c;
}
