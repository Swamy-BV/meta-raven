// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <optional>

namespace raven::tracking {
using GroundPoint = std::array<double, 2>;

// Constant-velocity ground-plane association prior, never a flight command or
// a published observation. Independent axes use the same measurement/process
// noise; three covariance terms suffice without a dense matrix solver.
class GroundMotion final {
 public:
  [[nodiscard]] bool Initialized() const { return stamp_.has_value(); }

  bool Observe(const GroundPoint& point, double stamp, double retention);
  [[nodiscard]] std::optional<double> Distance(const GroundPoint& point,
                                               double stamp,
                                               double retention) const;

 private:
  struct Covariance {
    double position = 0;
    double cross = 0;
    double velocity = 0;
  };

  struct Prediction {
    GroundPoint position{};
    Covariance covariance{};
  };

  [[nodiscard]] Prediction Predict(double dt) const;
  void Initialize(const GroundPoint& point, double stamp);
  GroundPoint position_{};
  GroundPoint velocity_{};
  Covariance covariance_;
  std::optional<double> stamp_;
};
}  // namespace raven::tracking
