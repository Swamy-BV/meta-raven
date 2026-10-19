// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>

namespace raven::media {
constexpr auto kMaxPhotos = 10000;
constexpr auto kExposureTimeUs = 10000;
constexpr auto kPhotoHeight = 1080;
constexpr auto kVideoWidth = 1280;
constexpr auto kFrameRate = 15;
constexpr auto kPhotoWidth = 1920;
constexpr auto kVideoBitrateKbps = 2000;
constexpr auto kImageHeight = 360;
constexpr auto kColourTemperature = 5000;
constexpr auto kStreamPort = 5600;
constexpr auto kSegmentSeconds = 60;
constexpr auto kImageWidth = 640;
constexpr auto kVideoHeight = 720;
constexpr auto kJpegQuality = 90;

struct Profile {
  std::string purpose;
  int width = kImageWidth;
  int height = kImageHeight;
  int fps = kFrameRate;
  std::string codec = "h264";
  int bitrate_kbps = kVideoBitrateKbps;
  int quality = kJpegQuality;
};

struct Source {
  std::string kind = "test";
  std::string uri;
  std::string camera_name;
  std::string image_topic = "/camera/image_raw";
  std::string camera_info_topic = "/camera/camera_info";
  int width = 0;
  int height = 0;
  bool loop = false;
};

struct Controls {
  bool auto_exposure = true;
  std::uint32_t exposure_time_us = kExposureTimeUs;
  bool auto_white_balance = true;
  std::uint32_t colour_temperature = kColourTemperature;
};

struct Settings {
  Source source;
  Profile photo{"photo", kPhotoWidth,       kPhotoHeight, kFrameRate,
                "jpeg",  kVideoBitrateKbps, kJpegQuality};
  Profile preview{"preview", kVideoWidth,       kVideoHeight, kFrameRate,
                  "h264",    kVideoBitrateKbps, kJpegQuality};
  Profile video{"video", kVideoWidth,       kVideoHeight, kFrameRate,
                "h264",  kVideoBitrateKbps, kJpegQuality};
  Profile image{"ros",  kImageWidth,       kImageHeight, kFrameRate,
                "rgb8", kVideoBitrateKbps, kJpegQuality};
  std::string mode = "video";
  std::string converter = "software";
  std::string encoder = "v4l2h264enc";
  std::string output_dir = "media";
  std::string stream_host = "127.0.0.1";
  int stream_port = kStreamPort;
  int max_photos = kMaxPhotos;
  int segment_seconds = kSegmentSeconds;
  bool recording = false;
  bool streaming = false;
};

struct Photo {
  std::string request_id;
  std::string uri;
  std::string frame_id;
  std::uint64_t stamp_us = 0;
  std::uint32_t index = 0;
  int width = 0;
  int height = 0;
  std::uint64_t source_stamp_ns = 0;
};

struct State {
  bool ready = false;
  bool capturing = false;
  std::uint32_t image_count = 0;
  std::string recording_id;
  std::string error;
};

void Validate(const Settings& settings);
[[nodiscard]] Profile& SelectProfile(Settings& settings,
                                     const std::string& purpose);
[[nodiscard]] bool SameVideoFormat(const Profile& left, const Profile& right);
}  // namespace raven::media
