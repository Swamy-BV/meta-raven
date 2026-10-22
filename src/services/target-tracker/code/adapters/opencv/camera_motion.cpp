// SPDX-License-Identifier: AGPL-3.0-only
#include "camera_motion.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ratio>
#include <utility>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

namespace raven::tracking {
namespace {
constexpr std::size_t kMaximumFrames = 16;
constexpr auto kMaximumFrameAgeNs = 1000000000LL;
constexpr auto kMaximumStepNs = 250000000LL;
constexpr int kMaximumCorners = 200;
constexpr int kFlowWindow = 15;
constexpr int kFlowPyramidLevels = 2;
constexpr int kFlowIterations = 10;
constexpr double kFlowEpsilon = 0.03;
constexpr int kMaskEnabled = 255;
constexpr int kAffineColumns = 3;
constexpr std::size_t kMinimumPoints = 25;
constexpr double kCornerQuality = 0.01;
constexpr double kCornerSpacing = 5;
constexpr double kMaskPadding = 2;
constexpr float kMaximumFlowError = 30.0F;
constexpr double kMaximumRoundTripError = 1;
constexpr double kMinimumInlierFraction = 0.5;
constexpr double kMinimumScale = 0.85;
constexpr double kMaximumScale = 1.15;
constexpr double kMaximumRotation = 0.2;
constexpr double kMaximumTranslationFraction = 0.25;

cv::Mat BackgroundMask(const GrayFrame& frame,
                       const std::vector<Observation>& observations) {
  // OpenCV defines signed pixel-type constants with bitwise macros.
  // NOLINTNEXTLINE(hicpp-signed-bitwise)
  cv::Mat mask(frame.gray.size(), CV_8UC1, cv::Scalar(kMaskEnabled));
  const auto sx = static_cast<double>(frame.gray.cols) / frame.source_width;
  const auto sy = static_cast<double>(frame.gray.rows) / frame.source_height;
  for (const auto& observation : observations) {
    const auto& box = observation.box;
    if (!std::isfinite(box.center_x) || !std::isfinite(box.center_y) ||
        !std::isfinite(box.width) || !std::isfinite(box.height) ||
        box.width <= 0 || box.height <= 0) {
      continue;  // The tracker validates and rejects malformed detections.
    }
    const auto edge = [](const double value, const int maximum) {
      return static_cast<int>(
          std::clamp(value, 0.0, static_cast<double>(maximum)));
    };
    const auto left =
        edge(std::floor((box.center_x - box.width / 2) * sx) - kMaskPadding,
             frame.gray.cols);
    const auto top =
        edge(std::floor((box.center_y - box.height / 2) * sy) - kMaskPadding,
             frame.gray.rows);
    const auto right =
        edge(std::ceil((box.center_x + box.width / 2) * sx) + kMaskPadding,
             frame.gray.cols);
    const auto bottom =
        edge(std::ceil((box.center_y + box.height / 2) * sy) + kMaskPadding,
             frame.gray.rows);
    if (right > left && bottom > top) {
      mask(cv::Rect(left, top, right - left, bottom - top)).setTo(0);
    }
  }
  return mask;
}

using Points = std::vector<cv::Point2f>;

struct Correspondences {
  Points before;
  Points after;
};

// Non-owning views valid for the duration of one optical-flow estimate.
struct FlowInput {
  const cv::Mat& before;
  const cv::Mat& after;
  const cv::Mat& before_mask;
  const cv::Mat& after_mask;
};

Correspondences BackgroundFlow(const FlowInput& input) {
  const auto& before = input.before;
  const auto& after = input.after;
  const auto& before_mask = input.before_mask;
  const auto& after_mask = input.after_mask;
  Points corners;
  Points forward;
  Points backward;
  cv::goodFeaturesToTrack(before, corners, kMaximumCorners, kCornerQuality,
                          kCornerSpacing, before_mask);
  if (corners.size() < kMinimumPoints) {
    return {};
  }
  std::vector<unsigned char> valid;
  std::vector<unsigned char> reverse_valid;
  std::vector<float> error;
  std::vector<float> reverse_error;
  const auto window = cv::Size(kFlowWindow, kFlowWindow);
  const auto criteria =
      cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
                       kFlowIterations, kFlowEpsilon);
  cv::calcOpticalFlowPyrLK(before, after, corners, forward, valid, error,
                           window, kFlowPyramidLevels, criteria);
  cv::calcOpticalFlowPyrLK(after, before, forward, backward, reverse_valid,
                           reverse_error, window, kFlowPyramidLevels, criteria);
  Correspondences result;
  for (std::size_t i = 0; i < corners.size(); ++i) {
    const auto& point = forward[i];
    if (valid[i] == 0 || reverse_valid[i] == 0 ||
        error[i] > kMaximumFlowError || !std::isfinite(point.x) ||
        !std::isfinite(point.y) || !std::isfinite(backward[i].x) ||
        !std::isfinite(backward[i].y) || point.x < 0 || point.y < 0 ||
        point.x >= static_cast<float>(after.cols) ||
        point.y >= static_cast<float>(after.rows) ||
        cv::norm(corners[i] - backward[i]) > kMaximumRoundTripError ||
        after_mask.at<unsigned char>(static_cast<int>(point.y),
                                     static_cast<int>(point.x)) == 0) {
      continue;
    }
    result.before.push_back(corners[i]);
    result.after.push_back(point);
  }
  return result;
}

CameraMotionResult Fit(const GrayFrame& before, const GrayFrame& after,
                       const cv::Mat& before_mask, const cv::Mat& after_mask) {
  const auto points =
      BackgroundFlow({before.gray, after.gray, before_mask, after_mask});
  if (points.before.size() < kMinimumPoints) {
    return {std::nullopt, "insufficient-background-features", 0, 0};
  }
  cv::Mat inliers;
  constexpr double kRansacPixels = 2;
  constexpr std::size_t kRansacIterations = 1000;
  constexpr double kRansacConfidence = 0.99;
  const auto matrix = cv::estimateAffinePartial2D(
      points.before, points.after, inliers, cv::RANSAC, kRansacPixels,
      kRansacIterations, kRansacConfidence);
  if (matrix.empty()) {
    return {std::nullopt, "background-fit-failed", 0, 0};
  }
  const auto count = static_cast<std::size_t>(cv::countNonZero(inliers));
  if (count < kMinimumPoints ||
      static_cast<double>(count) / static_cast<double>(points.before.size()) <
          kMinimumInlierFraction) {
    return {std::nullopt, "unreliable-background-fit", count, 0};
  }
  const auto scale =
      std::hypot(matrix.at<double>(0, 0), matrix.at<double>(1, 0));
  const auto rotation =
      std::atan2(matrix.at<double>(1, 0), matrix.at<double>(0, 0));
  if (!cv::checkRange(matrix) || scale < kMinimumScale ||
      scale > kMaximumScale || std::abs(rotation) > kMaximumRotation ||
      std::hypot(matrix.at<double>(0, 2), matrix.at<double>(1, 2)) >
          after.gray.cols * kMaximumTranslationFraction) {
    return {std::nullopt, "camera-motion-outside-envelope", count, 0};
  }
  CameraTransform transform{};
  const auto ratio = static_cast<double>(after.source_width) / after.gray.cols;
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col < kAffineColumns; ++col) {
      transform[static_cast<std::size_t>(row) * kAffineColumns +
                static_cast<std::size_t>(col)] =
          static_cast<float>(matrix.at<double>(row, col) *
                             (col == 2 ? ratio : 1));
    }
  }
  return {transform, "applied", count, 0};
}
}  // namespace

void CameraMotionEstimator::Push(GrayFrame frame) {
  if (!frames_.empty() && frame.stamp_ns <= frames_.back().stamp_ns) {
    return;
  }
  frames_.push_back(std::move(frame));
  while (frames_.size() > kMaximumFrames ||
         frames_.back().stamp_ns - frames_.front().stamp_ns >
             kMaximumFrameAgeNs) {
    frames_.pop_front();
  }
}

void CameraMotionEstimator::Reset() {
  frames_.clear();
  previous_.reset();
}

CameraMotionResult CameraMotionEstimator::Estimate(
    const std::int64_t stamp_ns, const std::string& frame_id,
    const std::vector<Observation>& observations) {
  const auto begin = std::chrono::steady_clock::now();
  const auto found = std::find_if(
      frames_.begin(), frames_.end(), [stamp_ns, &frame_id](const auto& frame) {
        return frame.stamp_ns == stamp_ns && frame.frame_id == frame_id;
      });
  if (found == frames_.end()) {
    previous_.reset();
    return {std::nullopt, "matching-image-unavailable", 0, 0};
  }
  MaskedFrame current{*found, BackgroundMask(*found, observations)};
  CameraMotionResult result{std::nullopt, "initializing", 0, 0};
  if (previous_ && previous_->image.frame_id == frame_id &&
      previous_->image.source_width == found->source_width &&
      previous_->image.source_height == found->source_height &&
      previous_->image.gray.size() == found->gray.size() &&
      stamp_ns > previous_->image.stamp_ns &&
      stamp_ns - previous_->image.stamp_ns <= kMaximumStepNs) {
    result =
        Fit(previous_->image, current.image, previous_->mask, current.mask);
  }
  previous_ = std::move(current);
  result.milliseconds = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - begin)
                            .count();
  return result;
}
}  // namespace raven::tracking
