// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raven::contracts {
inline constexpr std::size_t kMaxCameraBytes = 8ULL * 1024 * 1024;

// Packed RGB8 pixels. Unix and monotonic timestamps are in microseconds.
// PTS is optional; standard ROS Image transports do not carry source PTS.
struct CameraFrame {
  std::vector<std::uint8_t> data;
  std::uint64_t sequence = 0, captured_us = 0, pts_ns = 0;
  std::uint64_t captured_unix_us = 0;
  bool pts_valid = false, source_time_mapped = false;
  int width = 0, height = 0;
  std::string optical_frame;
};
}  // namespace raven::contracts
