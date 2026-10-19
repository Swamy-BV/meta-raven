// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <stddef.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace detection {
inline constexpr double kDefaultScoreThreshold = 0.5;
inline constexpr double kSuppressionIouThreshold = 0.45;
inline constexpr size_t kMaxCandidates = 30000;
inline constexpr size_t kMaxDetections = 100;

struct Box {
  double xmin, ymin, xmax, ymax, score;
  int cls;
};

inline double Sigmoid(double x) {
  if (!std::isfinite(x)) {
    throw std::runtime_error("nonfinite detector logit");
  }
  return x >= 0 ? 1.0 / (1.0 + std::exp(-x))
                : std::exp(x) / (1.0 + std::exp(x));
}

inline double Iou(const Box& a, const Box& b) {
  double const overlap =
      std::max(0.0, std::min(a.xmax, b.xmax) - std::max(a.xmin, b.xmin)) *
      std::max(0.0, std::min(a.ymax, b.ymax) - std::max(a.ymin, b.ymin));
  double const area = (a.xmax - a.xmin) * (a.ymax - a.ymin) +
                      (b.xmax - b.xmin) * (b.ymax - b.ymin) - overlap;
  return area > 0 ? overlap / area : 0;
}

inline nlohmann::json Suppress(const std::vector<Box>& boxes,
                               const std::vector<std::string>& labels,
                               double threshold = kDefaultScoreThreshold) {
  if (boxes.size() > kMaxCandidates) {
    throw std::runtime_error("detector candidate limit exceeded");
  }
  std::vector<Box> valid;
  for (auto b : boxes) {
    if (!std::isfinite(b.xmin) || !std::isfinite(b.ymin) ||
        !std::isfinite(b.xmax) || !std::isfinite(b.ymax) ||
        !std::isfinite(b.score) || b.cls < 0 ||
        static_cast<size_t>(b.cls) >= labels.size() || b.score < 0 ||
        b.score > 1) {
      throw std::runtime_error("invalid detector candidate");
    }
    b.xmin = std::clamp(b.xmin, 0.0, 1.0);
    b.ymin = std::clamp(b.ymin, 0.0, 1.0);
    b.xmax = std::clamp(b.xmax, 0.0, 1.0);
    b.ymax = std::clamp(b.ymax, 0.0, 1.0);
    if (b.score >= threshold && b.xmax > b.xmin && b.ymax > b.ymin) {
      valid.push_back(b);
    }
  }
  std::stable_sort(valid.begin(), valid.end(), [](const Box& a, const Box& b) {
    return a.score > b.score;
  });
  std::vector<Box> kept;
  nlohmann::json result = nlohmann::json::array();
  for (const auto& b : valid) {
    bool const duplicate =
        std::any_of(kept.begin(), kept.end(), [&](const Box& k) {
          return k.cls == b.cls && Iou(k, b) > kSuppressionIouThreshold;
        });
    if (duplicate) {
      continue;
    }
    kept.push_back(b);
    result.push_back({{"class_id", b.cls},
                      {"label", labels[b.cls]},
                      {"score", b.score},
                      {"box",
                       {{"xmin", b.xmin},
                        {"ymin", b.ymin},
                        {"xmax", b.xmax},
                        {"ymax", b.ymax}}}});
    if (kept.size() == kMaxDetections) {
      break;
    }
  }
  return result;
}
}  // namespace detection
