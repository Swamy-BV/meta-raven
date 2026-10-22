// SPDX-License-Identifier: AGPL-3.0-only
#include "adapters/opencv/camera_motion.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {
using raven::tracking::CameraMotionEstimator;

void Require(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void SimilarityAndForeground(const double translation) {
  cv::Mat background(180, 320, CV_8UC1);
  cv::RNG random(12345);
  background.setTo(30);
  for (int y = 15; y < background.rows - 15; y += 15) {
    for (int x = 15; x < background.cols - 15; x += 15) {
      cv::circle(
          background,
          cv::Point(x + random.uniform(-3, 4), y + random.uniform(-3, 4)),
          random.uniform(3, 6), cv::Scalar(random.uniform(80, 240)),
          cv::FILLED);
    }
  }
  cv::GaussianBlur(background, background, cv::Size(3, 3), 0.8);
  constexpr double kAngle = 0.02;
  constexpr double kScale = 1.01;
  const cv::Mat matrix =
      (cv::Mat_<double>(2, 3) << kScale * std::cos(kAngle),
       -kScale * std::sin(kAngle), translation, kScale * std::sin(kAngle),
       kScale * std::cos(kAngle), -3);
  cv::Mat after;
  cv::warpAffine(background, after, matrix, background.size());
  cv::rectangle(background, cv::Rect(140, 70, 40, 40), cv::Scalar(255),
                cv::FILLED);
  cv::rectangle(after, cv::Rect(170, 70, 40, 40), cv::Scalar(255), cv::FILLED);
  CameraMotionEstimator estimator;
  estimator.Push({1000000000, "camera", 640, 360, background});
  const auto first = estimator.Estimate(1000000000, "camera",
                                        {{{320, 180, 80, 80}, "2", 0.9}});
  Require(!first.transform, "First image fabricated a camera transform");
  estimator.Push({1100000000, "camera", 640, 360, after});
  const auto result = estimator.Estimate(1100000000, "camera",
                                         {{{380, 180, 80, 80}, "2", 0.9}});
  if (!result.transform) {
    throw std::runtime_error(
        "Known background motion rejected: " + result.status +
        " inliers=" + std::to_string(result.inliers));
  }
  const auto& actual = *result.transform;
  const std::array<double, 6> expected{
      kScale * std::cos(kAngle), -kScale * std::sin(kAngle), 2 * translation,
      kScale * std::sin(kAngle), kScale * std::cos(kAngle),  -6};
  for (std::size_t index = 0; index < actual.size(); ++index) {
    const auto tolerance = index == 2 || index == 5 ? 1.5 : 0.02;
    Require(std::abs(actual[index] - expected[index]) < tolerance,
            "Incorrect source-resolution camera similarity");
  }
  Require(result.inliers >= 25, "Insufficient verified background support");
  Require(!estimator.Estimate(1100000001, "camera", {}).transform,
          "Image acquisition timestamp mismatch accepted");
}

void RejectUnobservableMotion() {
  CameraMotionEstimator estimator;
  const cv::Mat constant(180, 320, CV_8UC1, cv::Scalar(127));
  estimator.Push({1000000000, "camera", 640, 360, constant});
  estimator.Estimate(1000000000, "camera", {});
  estimator.Push({1100000000, "camera", 640, 360, constant});
  const auto empty = estimator.Estimate(1100000000, "camera", {});
  Require(
      !empty.transform && empty.status == "insufficient-background-features",
      "Featureless scene fabricated camera motion");
  estimator.Push({1500000000, "camera", 640, 360, constant});
  const auto stale = estimator.Estimate(1500000000, "camera", {});
  Require(!stale.transform && stale.status == "initializing",
          "Long camera gap accepted");
  Require(!estimator.Estimate(1500000000, "different-camera", {}).transform,
          "Different optical frame accepted");
}
}  // namespace

int main() {
  try {
    SimilarityAndForeground(6);
    SimilarityAndForeground(12);
    RejectUnobservableMotion();
    std::cout
        << "Background motion, masking, frame/time and texture gates passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
