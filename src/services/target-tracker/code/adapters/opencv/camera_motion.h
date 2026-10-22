// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "core/tracker.h"

namespace raven::tracking {
struct GrayFrame {
  std::int64_t stamp_ns = 0;
  std::string frame_id;
  int source_width = 0;
  int source_height = 0;
  cv::Mat gray;
};

struct CameraMotionResult {
  std::optional<CameraTransform> transform;
  std::string status = "disabled";
  std::size_t inliers = 0;
  double milliseconds = 0;
};

// Serial access only. Retains at most sixteen reduced grayscale images and
// matches detection acquisition time exactly. Similarities compensate global
// image motion, not depth-dependent parallax or independently moving objects.
class CameraMotionEstimator {
 public:
  void Push(GrayFrame frame);
  void Reset();
  CameraMotionResult Estimate(std::int64_t stamp_ns,
                              const std::string& frame_id,
                              const std::vector<Observation>& observations);

 private:
  struct MaskedFrame {
    GrayFrame image;
    cv::Mat mask;
  };

  std::deque<GrayFrame> frames_;
  std::optional<MaskedFrame> previous_;
};
}  // namespace raven::tracking
