// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include <Eigen/Geometry>

namespace raven::navigation {

struct TimedPose {
  std::int64_t stamp_ns = 0;
  Eigen::Isometry3d world_from_body = Eigen::Isometry3d::Identity();
};

// Acquisition-time poses only. Interpolation never extrapolates or crosses a
// gap larger than max_gap_ns. Call Clear after either estimator resets.
class PoseHistory {
 public:
  PoseHistory(std::size_t capacity, std::int64_t max_gap_ns);
  [[nodiscard]] bool Push(const TimedPose& pose);
  [[nodiscard]] std::optional<TimedPose> At(std::int64_t stamp_ns) const;
  void Clear();

 private:
  std::size_t capacity_;
  std::int64_t max_gap_ns_;
  std::deque<TimedPose> poses_;
};

[[nodiscard]] bool ValidTransform(const Eigen::Isometry3d& transform);
[[nodiscard]] std::optional<std::int64_t> AcquisitionNanoseconds(
    std::uint64_t microseconds);
[[nodiscard]] bool FreshAcquisition(std::int64_t stamp_ns, std::int64_t now_ns,
                                    std::int64_t max_age_ns,
                                    std::int64_t future_tolerance_ns);

// ROS FLU body in a z-up local world to PX4 FRD body in NED, or the inverse.
// Position conversion alone is insufficient: both quaternion frames change.
[[nodiscard]] Eigen::Isometry3d NedFrdToEnuFlu(
    const Eigen::Isometry3d& ned_from_frd);

// One alignment per estimator session. Later paired observations verify the
// alignment; they never move the map origin to conceal drift or a reset.
class FrameAlignment {
 public:
  FrameAlignment(double max_translation_error, double max_rotation_error);
  [[nodiscard]] bool Observe(const TimedPose& reference, const TimedPose& local,
                             std::uint8_t reference_reset);
  [[nodiscard]] std::optional<Eigen::Isometry3d> Transform() const;
  [[nodiscard]] bool Faulted() const;
  void Invalidate();
  void Reset();

 private:
  double max_translation_error_;
  double max_rotation_error_;
  std::optional<Eigen::Isometry3d> reference_from_local_;
  std::optional<std::uint8_t> reset_counter_;
  bool faulted_ = false;
};

}  // namespace raven::navigation
