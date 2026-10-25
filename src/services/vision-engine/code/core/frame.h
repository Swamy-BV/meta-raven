// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace raven::vision {
// Immutable RGB8 view. The aliasing owner retains the source ROS message
// throughout the job; stride includes any source row padding.
struct Frame {
  std::shared_ptr<const std::uint8_t> pixels;
  std::size_t bytes = 0;
  int width = 0, height = 0, stride = 0;
  std::uint64_t sequence = 0, captured_us = 0, captured_unix_us = 0;
  std::uint64_t source_stamp_ns = 0, pts_ns = 0;
  bool source_time_mapped = false, pts_valid = false;
  std::string optical_frame;
};
}  // namespace raven::vision
