// SPDX-License-Identifier: AGPL-3.0-only

#include <mavlink/v2.0/common/mavlink.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "link.h"
#include "node.h"
#include "raven_interfaces/msg/media_capture_event.hpp"
#include "raven_interfaces/msg/media_profile.hpp"

namespace raven::camera {
namespace {
constexpr auto kBytesPerMib = 1024.0F * 1024.0F;
constexpr auto kBitsPerKbit = 1000U;
constexpr auto kMicrosPerSecond = 1000000ULL;
constexpr auto kNanosPerMicro = 1000U;
constexpr auto kUnknownPosition = std::numeric_limits<std::int32_t>::max();
constexpr auto kUnknownOptics = std::numeric_limits<float>::quiet_NaN();

template <class Character, std::size_t Size>
void Text(Character (&destination)[Size], const std::string& value) {
  std::copy_n(value.begin(), std::min(value.size(), Size - 1), destination);
}
}  // namespace

bool CameraNode::Publish(std::uint32_t message_id) {
  switch (message_id) {
    case MAVLINK_MSG_ID_CAMERA_INFORMATION:
      CameraInformation();
      return true;
    case MAVLINK_MSG_ID_CAMERA_SETTINGS:
      CameraSettings();
      return true;
    case MAVLINK_MSG_ID_STORAGE_INFORMATION:
      StorageInformation();
      return true;
    case MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS:
      CaptureStatus();
      return true;
    case MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION:
      if (stream_uri_.empty()) {
        return false;
      }
      StreamInformation();
      return true;
    case MAVLINK_MSG_ID_VIDEO_STREAM_STATUS:
      StreamStatus();
      return true;
    default:
      return false;
  }
}

void CameraNode::CameraInformation() {
  mavlink_camera_information_t packet{};
  packet.time_boot_ms = BootMilliseconds();
  packet.firmware_version = 1;
  packet.focal_length = kUnknownOptics;
  packet.sensor_size_h = kUnknownOptics;
  packet.sensor_size_v = kUnknownOptics;
  packet.resolution_h = static_cast<std::uint16_t>(capabilities_.max_width);
  packet.resolution_v = static_cast<std::uint16_t>(capabilities_.max_height);
  packet.flags = static_cast<unsigned>(CAMERA_CAP_FLAGS_CAPTURE_IMAGE) |
                 static_cast<unsigned>(CAMERA_CAP_FLAGS_CAPTURE_VIDEO) |
                 static_cast<unsigned>(CAMERA_CAP_FLAGS_HAS_MODES);
  if (capabilities_.photo_during_recording) {
    packet.flags |=
        static_cast<unsigned>(CAMERA_CAP_FLAGS_CAN_CAPTURE_IMAGE_IN_VIDEO_MODE);
  }
  if (!stream_uri_.empty()) {
    packet.flags |= static_cast<unsigned>(CAMERA_CAP_FLAGS_HAS_VIDEO_STREAM);
  }
  Text(packet.vendor_name, "Raven");
  Text(packet.model_name, "ROS media capture");
  link_.Send(packet, mavlink_msg_camera_information_encode_status);
}

void CameraNode::CameraSettings() {
  mavlink_camera_settings_t packet{};
  packet.time_boot_ms = BootMilliseconds();
  packet.mode_id =
      state_.mode == "photo" ? CAMERA_MODE_IMAGE : CAMERA_MODE_VIDEO;
  packet.zoomLevel = kUnknownOptics;
  packet.focusLevel = kUnknownOptics;
  link_.Send(packet, mavlink_msg_camera_settings_encode_status);
}

void CameraNode::CaptureStatus() {
  mavlink_camera_capture_status_t packet{};
  packet.time_boot_ms = BootMilliseconds();
  packet.image_status = Fresh() && state_.capturing ? 1 : 0;
  packet.video_status = Fresh() && state_.recording ? 1 : 0;
  packet.image_interval = photo_interval_;
  packet.available_capacity =
      static_cast<float>(state_.free_bytes) / kBytesPerMib;
  packet.image_count = static_cast<std::int32_t>(std::min(
      state_.image_count,
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
  if (packet.video_status != 0) {
    packet.recording_time_ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recording_started_)
            .count());
  }
  link_.Send(packet, mavlink_msg_camera_capture_status_encode_status);
}

void CameraNode::StorageInformation() {
  mavlink_storage_information_t packet{};
  packet.time_boot_ms = BootMilliseconds();
  packet.storage_id = 1;
  packet.storage_count = 1;
  packet.status = Fresh() ? STORAGE_STATUS_READY : STORAGE_STATUS_NOT_SUPPORTED;
  packet.type = STORAGE_TYPE_OTHER;
  packet.total_capacity = static_cast<float>(state_.total_bytes) / kBytesPerMib;
  packet.available_capacity =
      static_cast<float>(state_.free_bytes) / kBytesPerMib;
  packet.used_capacity = packet.total_capacity - packet.available_capacity;
  Text(packet.name, "Media storage");
  link_.Send(packet, mavlink_msg_storage_information_encode_status);
}

raven_interfaces::msg::MediaProfile CameraNode::VideoProfile() const {
  const auto purpose = state_.recording ? "video" : "preview";
  for (const auto& profile : state_.profiles) {
    if (profile.purpose == purpose) {
      return profile;
    }
  }
  return raven_interfaces::msg::MediaProfile{};
}

void CameraNode::StreamInformation() {
  const auto profile = VideoProfile();
  mavlink_video_stream_information_t packet{};
  packet.stream_id = 1;
  packet.count = 1;
  packet.type = VIDEO_STREAM_TYPE_RTSP;
  packet.flags =
      Fresh() && state_.streaming ? VIDEO_STREAM_STATUS_FLAGS_RUNNING : 0;
  packet.framerate = static_cast<float>(profile.fps);
  packet.bitrate = profile.bitrate_kbps * kBitsPerKbit;
  packet.resolution_h = static_cast<std::uint16_t>(profile.width);
  packet.resolution_v = static_cast<std::uint16_t>(profile.height);
  packet.encoding = VIDEO_STREAM_ENCODING_H264;
  Text(packet.name, "Camera preview");
  Text(packet.uri, stream_uri_);
  link_.Send(packet, mavlink_msg_video_stream_information_encode_status);
}

void CameraNode::StreamStatus() {
  const auto profile = VideoProfile();
  mavlink_video_stream_status_t packet{};
  packet.stream_id = 1;
  packet.flags =
      Fresh() && state_.streaming ? VIDEO_STREAM_STATUS_FLAGS_RUNNING : 0;
  packet.framerate = static_cast<float>(profile.fps);
  packet.bitrate = profile.bitrate_kbps * kBitsPerKbit;
  packet.resolution_h = static_cast<std::uint16_t>(profile.width);
  packet.resolution_v = static_cast<std::uint16_t>(profile.height);
  link_.Send(packet, mavlink_msg_video_stream_status_encode_status);
}

void CameraNode::Captured(
    const raven_interfaces::msg::MediaCaptureEvent& event) {
  mavlink_camera_image_captured_t packet{};
  packet.time_boot_ms = BootMilliseconds();
  packet.time_utc =
      static_cast<std::uint64_t>(event.header.stamp.sec) * kMicrosPerSecond +
      event.header.stamp.nanosec / kNanosPerMicro;
  // This adapter has no calibrated camera pose or geotagging provider yet.
  packet.lat = kUnknownPosition;
  packet.lon = kUnknownPosition;
  packet.alt = kUnknownPosition;
  packet.relative_alt = kUnknownPosition;
  std::fill(std::begin(packet.q), std::end(packet.q), kUnknownOptics);
  packet.image_index = event.image_index == 0
                           ? -1
                           : static_cast<std::int32_t>(event.image_index - 1);
  packet.capture_result = event.success ? 1 : 0;
  Text(packet.file_url, event.uri);
  link_.Send(packet, mavlink_msg_camera_image_captured_encode_status);
}
}  // namespace raven::camera
