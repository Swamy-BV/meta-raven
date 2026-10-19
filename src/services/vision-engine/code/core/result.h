// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace raven::vision {
struct Identity {
  std::string instance, model, version, sha;
  std::uint64_t generation = 0;
};

struct Hypothesis {
  std::uint16_t class_id = 0;
  double score = 0;
};

struct Detection {
  Hypothesis hypothesis;
  double center_x = 0, center_y = 0, width = 0, height = 0;
};

struct Result {
  Identity identity;
  std::uint64_t job_sequence = 0, frame_sequence = 0, stamp_us = 0,
                stamp_ns = 0;
  std::uint64_t captured_monotonic_us = 0, pts_ns = 0;
  bool pts_valid = false, source_time_mapped = false;
  std::uint32_t width = 0, height = 0;
  std::string task, optical_frame;
  std::vector<Hypothesis> classification;
  std::vector<Detection> detections;
};

struct Snapshot {
  std::optional<Identity> identity;
  std::vector<std::string> labels;
  std::optional<Result> result;
  std::string task, health;
  bool failed = false;
  float threshold = 0;
  std::vector<std::pair<std::string, std::string>> metrics;
};
}  // namespace raven::vision
