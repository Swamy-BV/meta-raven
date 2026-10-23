// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/ground_motion.h"

namespace raven::tracking {
struct Box {
  double center_x = 0;
  double center_y = 0;
  double width = 0;
  double height = 0;
};

struct Observation {
  Box box;
  std::string class_id;
  double score = 0;
  // Optional unit-length vehicle descriptor; empty means appearance
  // unavailable.
  std::vector<float> appearance;
  // Eligible for this embedding model even when its source image is missing.
  bool appearance_subject = false;
  // Optional calibrated ground-plane location at this image acquisition time.
  std::optional<GroundPoint> ground_point = std::nullopt;
};

struct AppearanceStatistics {
  std::size_t recoveries = 0;
  std::size_t rejections = 0;
};

// Reject invalid geometry, class, score or non-unit descriptors before adapters
// perform allocation or inference. Throws std::invalid_argument on failure.
void ValidateObservation(const Observation& observation);

struct Track {
  Observation observation;
  std::string id;
};

inline constexpr double kDefaultLostSeconds = 1.5;
inline constexpr double kDefaultLowThreshold = 0.1;
inline constexpr double kDefaultHighThreshold = 0.5;
inline constexpr double kDefaultNewThreshold = 0.7;
inline constexpr double kDefaultMatchThreshold = 0.8;
inline constexpr std::size_t kDefaultTracks = 32;
inline constexpr std::size_t kDefaultClasses = 8;
inline constexpr std::size_t kDefaultDetections = 128;

struct Configuration {
  bool class_agnostic = false;
  bool use_appearance = false;
  bool use_ground_motion = false;
  double lost_seconds = kDefaultLostSeconds;
  double low_threshold = kDefaultLowThreshold;
  double high_threshold = kDefaultHighThreshold;
  double new_threshold = kDefaultNewThreshold;
  double match_threshold = kDefaultMatchThreshold;
  std::size_t max_tracks_per_class = kDefaultTracks;
  std::size_t max_classes = kDefaultClasses;
  std::size_t max_detections = kDefaultDetections;
};

// One instance owns one camera stream. Call serially, with strictly increasing
// acquisition timestamps in nanoseconds. Outputs are measurement-backed tracks;
// temporarily lost predictions are retained internally, never published as
// seen. Optional class-agnostic association retains the category established
// at birth; its score is the last observation supporting that category. Raw
// detector hypotheses remain independent of this temporal track metadata.
class Tracker final {
 public:
  explicit Tracker(Configuration configuration);
  ~Tracker();
  Tracker(const Tracker&) = delete;
  Tracker& operator=(const Tracker&) = delete;
  Tracker(Tracker&&) = delete;
  Tracker& operator=(Tracker&&) = delete;
  std::vector<Track> Update(const std::vector<Observation>& observations,
                            std::int64_t stamp_ns);
  void Reset();
  [[nodiscard]] std::size_t RetainedCount() const;
  [[nodiscard]] AppearanceStatistics AppearanceHealth() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace raven::tracking
