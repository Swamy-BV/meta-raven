// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <string>

#include "types.h"

namespace raven::media {
namespace {
constexpr auto kMinimumDimension = 64;
constexpr auto kMinimumBitrate = 64;
constexpr auto kMaxWidth = 4096;
constexpr auto kMaxHeight = 3072;
constexpr auto kMaxFps = 60;
constexpr auto kMaxBitrate = 50000;
constexpr auto kMinPort = 1024;
constexpr auto kMaxPort = 65535;
constexpr auto kMaximumPhotoCount = 10000;
constexpr auto kMinQuality = 1;
constexpr auto kMaxQuality = 100;
constexpr auto kMaxSegmentSeconds = 3600;

void ValidateProfile(const Profile& profile) {
  if (profile.width < kMinimumDimension || profile.height < kMinimumDimension ||
      profile.width > kMaxWidth || profile.height > kMaxHeight ||
      profile.width % 2 || profile.height % 2 || profile.fps < 1 ||
      profile.fps > kMaxFps || profile.bitrate_kbps < kMinimumBitrate ||
      profile.bitrate_kbps > kMaxBitrate || profile.quality < kMinQuality ||
      profile.quality > kMaxQuality) {
    throw std::invalid_argument(
        "profile dimensions, rate or quality are out of bounds");
  }
}

void ValidateSource(const Source& source) {
  const std::array<std::string, 7> sources{
      "test", "libcamera", "camera", "image", "file", "ros", "rtp"};
  if (std::find(sources.begin(), sources.end(), source.kind) == sources.end()) {
    throw std::invalid_argument("unsupported source");
  }
  if ((source.kind == "camera" || source.kind == "image" ||
       source.kind == "file" || source.kind == "rtp") &&
      source.uri.empty()) {
    throw std::invalid_argument(
        "this source requires an input URI or device path");
  }
  if (source.kind == "rtp" && source.uri.rfind("udp://", 0) != 0) {
    throw std::invalid_argument("RTP input requires a udp:// address and port");
  }
  if (source.width < 0 || source.height < 0 || source.width > kMaxWidth ||
      source.height > kMaxHeight ||
      (source.width == 0) != (source.height == 0)) {
    throw std::invalid_argument(
        "source dimensions must be a bounded width/height pair");
  }
}
}  // namespace

Profile& SelectProfile(Settings& settings, const std::string& purpose) {
  for (auto* profile :
       {&settings.photo, &settings.preview, &settings.video, &settings.image}) {
    if (profile->purpose == purpose) {
      return *profile;
    }
  }
  throw std::invalid_argument("unknown profile purpose");
}

bool SameVideoFormat(const Profile& left, const Profile& right) {
  return left.width == right.width && left.height == right.height &&
         left.fps == right.fps && left.codec == right.codec &&
         left.bitrate_kbps == right.bitrate_kbps;
}

void Validate(const Settings& settings) {
  ValidateSource(settings.source);
  for (const auto* profile :
       {&settings.photo, &settings.preview, &settings.video, &settings.image}) {
    ValidateProfile(*profile);
  }
  if (settings.photo.codec != "jpeg" || settings.image.codec != "rgb8" ||
      settings.preview.codec != "h264" || settings.video.codec != "h264") {
    throw std::invalid_argument(
        "supported profiles are JPEG photos, RGB8 ROS and H.264 video");
  }
  if ((settings.mode != "photo" && settings.mode != "video") ||
      (settings.converter != "g2d" && settings.converter != "software") ||
      settings.encoder != "v4l2h264enc" || settings.output_dir.empty() ||
      settings.stream_port < kMinPort || settings.stream_port > kMaxPort ||
      settings.max_photos < 1 || settings.max_photos > kMaximumPhotoCount ||
      settings.segment_seconds < 2 ||
      settings.segment_seconds > kMaxSegmentSeconds) {
    throw std::invalid_argument("invalid capture configuration");
  }
  if (settings.recording && settings.streaming &&
      !SameVideoFormat(settings.video, settings.preview)) {
    throw std::invalid_argument(
        "one encoder requires matching active recording and preview profiles");
  }
  if (settings.source.kind == "libcamera" && settings.source.width > 0 &&
      (settings.photo.width > settings.source.width ||
       settings.photo.height > settings.source.height)) {
    throw std::invalid_argument(
        "photo profile exceeds the configured source dimensions");
  }
}
}  // namespace raven::media
