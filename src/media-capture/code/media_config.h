// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

inline constexpr int kDefaultRtpPort = 5602;
inline constexpr int kDefaultWidth = 640;
inline constexpr int kDefaultHeight = 360;
inline constexpr int kDefaultRate = 15;
inline constexpr int kDefaultBitrate = 1000;
inline constexpr int kDefaultStreamPort = 5600;
inline constexpr int kDefaultSegmentSeconds = 10;
inline constexpr int kDefaultSegmentCount = 12;
inline constexpr int kDefaultPhotoCount = 100;

// Configuration is supplied as ROS parameters; there are no private sockets.
struct Config {
  std::string source = "test", input, device = "/dev/video0";
  std::string host = "127.0.0.1", output = "media";
  int width = kDefaultWidth, height = kDefaultHeight, fps = kDefaultRate;
  int bitrate = kDefaultBitrate, port = kDefaultStreamPort;
  int segment = kDefaultSegmentSeconds, segments = kDefaultSegmentCount;
  int snapshots = kDefaultPhotoCount;
  bool recording = false, stream = false;
  std::string codec = "h264", converter = "g2d";
  std::string camera_name;
  std::string rtp_address = "127.0.0.1";
  int rtp_port = kDefaultRtpPort;
};
