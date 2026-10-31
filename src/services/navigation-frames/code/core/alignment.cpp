// SPDX-License-Identifier: AGPL-3.0-only
#include "alignment.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/LU>

namespace raven::navigation {

bool FreshAcquisition(const std::int64_t stamp_ns, const std::int64_t now_ns,
                      const std::int64_t max_age_ns,
                      const std::int64_t future_tolerance_ns) {
  if (stamp_ns <= 0 || now_ns <= 0 || max_age_ns <= 0 ||
      future_tolerance_ns < 0) {
    return false;
  }
  const auto age = now_ns - stamp_ns;
  return age >= -future_tolerance_ns && age <= max_age_ns;
}

std::optional<std::int64_t> AcquisitionNanoseconds(
    const std::uint64_t microseconds) {
  constexpr auto kNanosecondsPerMicrosecond = 1000LL;
  constexpr auto kMaximumMicroseconds = static_cast<std::uint64_t>(
      std::numeric_limits<std::int64_t>::max() / kNanosecondsPerMicrosecond);
  if (microseconds == 0 || microseconds > kMaximumMicroseconds) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(microseconds) * kNanosecondsPerMicrosecond;
}

bool ValidTransform(const Eigen::Isometry3d& transform) {
  constexpr double kTolerance = 1e-6;
  const Eigen::Matrix3d rotation = transform.linear();
  return transform.matrix().allFinite() && rotation.isUnitary(kTolerance) &&
         std::abs(rotation.determinant() - 1) < kTolerance &&
         transform.matrix().row(3).isApprox(Eigen::RowVector4d(0, 0, 0, 1),
                                            kTolerance);
}

PoseHistory::PoseHistory(const std::size_t capacity,
                         const std::int64_t max_gap_ns)
    : capacity_(capacity), max_gap_ns_(max_gap_ns) {
  if (capacity < 2 || max_gap_ns <= 0) {
    throw std::invalid_argument("Invalid acquisition-time history limits");
  }
}

bool PoseHistory::Push(const TimedPose& pose) {
  if (pose.stamp_ns <= 0 || !ValidTransform(pose.world_from_body) ||
      (!poses_.empty() && pose.stamp_ns <= poses_.back().stamp_ns)) {
    return false;
  }
  poses_.push_back(pose);
  if (poses_.size() > capacity_) {
    poses_.pop_front();
  }
  return true;
}

std::optional<TimedPose> PoseHistory::At(const std::int64_t stamp_ns) const {
  if (poses_.empty() || stamp_ns < poses_.front().stamp_ns ||
      stamp_ns > poses_.back().stamp_ns) {
    return std::nullopt;
  }
  const auto right =
      std::lower_bound(poses_.begin(), poses_.end(), stamp_ns,
                       [](const TimedPose& pose, const std::int64_t stamp) {
                         return pose.stamp_ns < stamp;
                       });
  if (right == poses_.end()) {
    return std::nullopt;
  }
  if (right->stamp_ns == stamp_ns) {
    return *right;
  }
  if (right == poses_.begin()) {
    return std::nullopt;
  }
  const auto left = std::prev(right);
  const auto gap = right->stamp_ns - left->stamp_ns;
  if (gap > max_gap_ns_) {
    return std::nullopt;
  }
  const double ratio =
      static_cast<double>(stamp_ns - left->stamp_ns) / static_cast<double>(gap);
  TimedPose result;
  result.stamp_ns = stamp_ns;
  result.world_from_body.translation() =
      (1 - ratio) * left->world_from_body.translation() +
      ratio * right->world_from_body.translation();
  result.world_from_body.linear() =
      Eigen::Quaterniond(left->world_from_body.linear())
          .slerp(ratio, Eigen::Quaterniond(right->world_from_body.linear()))
          .toRotationMatrix();
  return result;
}

void PoseHistory::Clear() { poses_.clear(); }

Eigen::Isometry3d NedFrdToEnuFlu(const Eigen::Isometry3d& ned_from_frd) {
  Eigen::Matrix3d enu_from_ned;
  enu_from_ned << 0, 1, 0, 1, 0, 0, 0, 0, -1;
  Eigen::Matrix3d frd_from_flu = Eigen::Matrix3d::Identity();
  frd_from_flu(1, 1) = -1;
  frd_from_flu(2, 2) = -1;
  Eigen::Isometry3d output = Eigen::Isometry3d::Identity();
  output.translation() = enu_from_ned * ned_from_frd.translation();
  output.linear() = enu_from_ned * ned_from_frd.linear() * frd_from_flu;
  return output;
}

FrameAlignment::FrameAlignment(const double max_translation_error,
                               const double max_rotation_error)
    : max_translation_error_(max_translation_error),
      max_rotation_error_(max_rotation_error) {
  if (!std::isfinite(max_translation_error) || max_translation_error <= 0 ||
      !std::isfinite(max_rotation_error) || max_rotation_error <= 0) {
    throw std::invalid_argument("Invalid frame alignment error limits");
  }
}

bool FrameAlignment::Observe(const TimedPose& reference, const TimedPose& local,
                             const std::uint8_t reference_reset) {
  if (faulted_ || reference.stamp_ns <= 0 ||
      reference.stamp_ns != local.stamp_ns ||
      !ValidTransform(reference.world_from_body) ||
      !ValidTransform(local.world_from_body)) {
    return false;
  }
  if (reset_counter_ && *reset_counter_ != reference_reset) {
    faulted_ = true;
    return false;
  }
  if (!reference_from_local_) {
    const Eigen::Isometry3d candidate =
        reference.world_from_body * local.world_from_body.inverse();
    if (!ValidTransform(candidate)) {
      return false;
    }
    reference_from_local_ = candidate;
    reset_counter_ = reference_reset;
    return true;
  }
  const auto predicted = *reference_from_local_ * local.world_from_body;
  const auto translation_error =
      (predicted.translation() - reference.world_from_body.translation())
          .norm();
  const Eigen::Quaterniond predicted_rotation(predicted.linear());
  const Eigen::Quaterniond reference_rotation(
      reference.world_from_body.linear());
  // q and -q represent the same rotation. The bounded dot product also avoids
  // normalizing a near-zero angle-axis vector when the frames agree.
  const auto cosine = std::clamp(
      std::abs(predicted_rotation.dot(reference_rotation)), 0.0, 1.0);
  const auto rotation_error = 2 * std::acos(cosine);
  if (translation_error > max_translation_error_ ||
      rotation_error > max_rotation_error_) {
    faulted_ = true;
    return false;
  }
  return true;
}

std::optional<Eigen::Isometry3d> FrameAlignment::Transform() const {
  return faulted_ ? std::nullopt : reference_from_local_;
}

bool FrameAlignment::Faulted() const { return faulted_; }

void FrameAlignment::Invalidate() { faulted_ = true; }

void FrameAlignment::Reset() {
  reference_from_local_.reset();
  reset_counter_.reset();
  faulted_ = false;
}

}  // namespace raven::navigation
