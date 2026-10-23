// SPDX-License-Identifier: AGPL-3.0-only
#include "appearance.h"

#include <BYTETracker.h>
#include <STrack.h>
#include <dataType.h>
#include <kalmanFilter.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Eigen/Core>

#include "core/tracker.h"

namespace raven::tracking {
namespace {
constexpr float kMaximumDistance = 0.3F;
constexpr double kAppearanceWeight = 0.5;
constexpr float kAmbiguityMargin = 0.05F;
constexpr float kRejected = 1.0F;
constexpr float kRecoveryCost = 0.6F;
constexpr float kMaximumRecoveryIouDistance = 0.8F;
constexpr double kMinimumRecoveryOverlap =
    1.0 - static_cast<double>(kMaximumRecoveryIouDistance);
constexpr double kGalleryInterval = 0.5;
constexpr double kMaximumRecoveryAspectChange = 1.5;
constexpr double kMinimumScore = 0.7;
constexpr double kContinuitySeconds = 0.3;
constexpr std::size_t kGallerySize = 8;
// DeepSORT's 95% chi-square gate for four observed coordinates.
constexpr float kMotionGate = 9.4877F;

double Overlap(const Box& left, const Box& right) {
  const auto intersection = [](double a, double length_a, double b,
                               double length_b) {
    return std::max(0.0, std::min(a + length_a / 2, b + length_b / 2) -
                             std::max(a - length_a / 2, b - length_b / 2));
  };
  const auto area =
      intersection(left.center_x, left.width, right.center_x, right.width) *
      intersection(left.center_y, left.height, right.center_y, right.height);
  return area / (left.width * left.height + right.width * right.height - area);
}

bool MotionAllows(const STrack& track, const Box& box) {
  byte_kalman::KalmanFilter filter;
  DETECTBOX observed;
  observed << static_cast<float>(box.center_x),
      static_cast<float>(box.center_y),
      static_cast<float>(box.width / box.height),
      static_cast<float>(box.height);
  const auto distance =
      filter.gating_distance(track.mean, track.covariance, {observed});
  return std::isfinite(distance[0]) && distance[0] <= kMotionGate;
}

bool Unique(const std::vector<std::vector<float>>& costs, std::size_t row,
            std::size_t column) {
  const auto candidate = costs[row][column];
  for (std::size_t other = 0; other < costs.size(); ++other) {
    if (other != row && costs[other][column] < candidate + kAmbiguityMargin) {
      return false;
    }
  }
  for (std::size_t other = 0; other < costs[row].size(); ++other) {
    if (other != column && costs[row][other] < candidate + kAmbiguityMargin) {
      return false;
    }
  }
  return true;
}
}  // namespace

AppearanceAssociation::AppearanceAssociation(bool ground_motion,
                                             double retention)
    : ground_motion_(ground_motion), retention_(retention) {}

AssociationHooks AppearanceAssociation::Bind(
    const std::vector<Observation>& observations,
    const std::vector<std::size_t>& indices, double stamp) {
  measurements_.clear();
  deferred_.clear();
  observations_ = &observations;
  stamp_ = stamp;
  indices_ = &indices;
  return {[this](auto& costs, const auto& tracks, const auto&,
                 const auto& ids) { Refine(costs, tracks, ids); },
          [this](const auto& track, const auto& detection) {
            Measured(track, detection);
          }};
}

const Observation& AppearanceAssociation::Measurement(std::size_t index) const {
  if (index >= indices_->size() ||
      (*indices_)[index] >= observations_->size()) {
    throw std::runtime_error(
        "ByteTrack measurement index exceeds input bounds");
  }
  return (*observations_)[(*indices_)[index]];
}

float AppearanceAssociation::Distance(
    const Gallery& gallery, const std::vector<float>& descriptor) const {
  auto best = kRejected;
  for (const auto& candidate : gallery.descriptors) {
    best = std::min(
        best, 1.0F - std::inner_product(candidate.begin(), candidate.end(),
                                        descriptor.begin(), 0.0F));
  }
  return best;
}

float AppearanceAssociation::CandidateDistance(
    const STrack& track, const Observation& observation) const {
  const auto found = galleries_.find(track.track_id);
  if (found == galleries_.end() || observation.appearance.empty()) {
    return kRejected;
  }
  const auto appearance = Distance(found->second, observation.appearance);
  const auto grounded = Grounded(found->second, observation);
  if ((ground_motion_ && track.state == TrackState::Lost) || grounded) {
    const auto motion = observation.ground_point
                            ? found->second.ground.Distance(
                                  *observation.ground_point, stamp_, retention_)
                            : std::nullopt;
    if (!motion || appearance > kMaximumDistance) {
      return kRejected;
    }
    // Compare positions in a pose-aligned frame; stale image coordinates are
    // not a motion prior after the camera has translated or rotated.
    return static_cast<float>(
        kAppearanceWeight * static_cast<double>(appearance) +
        (1 - kAppearanceWeight) * static_cast<double>(kMaximumDistance) *
            *motion);
  }
  return MotionAllows(track, observation.box) ? appearance : kRejected;
}

bool AppearanceAssociation::RecoveryAllows(const STrack& track, const Box& box,
                                           float proximity) const {
  if (track.state != TrackState::Lost) {
    return true;
  }
  const auto found = galleries_.find(track.track_id);
  if (ground_motion_) {
    if (found == galleries_.end()) {
      return false;
    }
    // A clipped vehicle box has no reliable ground contact point. Require
    // shape continuity before using its footpoint to recover an identity.
    const auto before =
        found->second.observed_box.width / found->second.observed_box.height;
    const auto ratio = box.width / box.height / before;
    return ratio >= 1 / kMaximumRecoveryAspectChange &&
           ratio <= kMaximumRecoveryAspectChange;
  }
  if (proximity <= kMaximumRecoveryIouDistance) {
    return true;
  }
  // Partial occlusion can corrupt image velocity. Check the last confident
  // observation as well as the prediction, while retaining the motion gate.
  return found != galleries_.end() &&
         Overlap(found->second.observed_box, box) >= kMinimumRecoveryOverlap;
}

bool AppearanceAssociation::Grounded(const Gallery& gallery,
                                     const Observation& observation) const {
  return ground_motion_ && observation.ground_point &&
         gallery.ground.Initialized();
}

float AppearanceAssociation::AssociationCost(const STrack& track,
                                             const Candidate& candidate) {
  const auto& measured = Measurement(candidate.input_index);
  const auto cost = CandidateDistance(track, measured);
  const auto shape_allowed =
      RecoveryAllows(track, measured.box, candidate.iou_distance);
  // A plausible retained identity gets a chance at a fuller observation before
  // this candidate may create a competing track. Association gates stay strict.
  if (track.state == TrackState::Lost && cost <= kMaximumDistance &&
      !shape_allowed) {
    deferred_.insert(candidate.input_index);
  }
  return shape_allowed ? cost : kRejected;
}

AppearanceAssociation::Costs AppearanceAssociation::BuildCosts(
    const std::vector<STrack*>& tracks, const std::vector<std::size_t>& indices,
    const Costs& proximity) {
  Costs result;
  result.reserve(tracks.size());
  for (const auto* track : tracks) {
    std::vector<float> row;
    row.reserve(indices.size());
    for (const auto index : indices) {
      row.push_back(AssociationCost(
          *track, {index, proximity[result.size()][row.size()]}));
    }
    result.push_back(std::move(row));
  }
  return result;
}

void AppearanceAssociation::ApplyRow(const std::vector<std::size_t>& indices,
                                     const Costs& appearance, std::size_t row,
                                     const STrack& track,
                                     std::vector<float>& costs) {
  if (galleries_.count(track.track_id) == 0) {
    return;
  }
  const bool recovering = track.state == TrackState::Lost;
  for (std::size_t column = 0; column < indices.size(); ++column) {
    const auto& measured = Measurement(indices[column]);
    if (!recovering && measured.appearance.empty()) {
      continue;
    }
    const auto distance = appearance[row][column];
    const auto grounded = Grounded(galleries_.at(track.track_id), measured);
    const auto ambiguous = distance <= kMaximumDistance &&
                           (recovering || grounded) &&
                           !Unique(appearance, row, column);
    if (recovering && ambiguous) {
      deferred_.insert(indices[column]);
    }
    if (distance > kMaximumDistance || ambiguous) {
      costs[column] = kRejected;
      ++rejections_;
    } else if (recovering || grounded) {
      costs[column] = kRecoveryCost * distance / kMaximumDistance;
    }
  }
}

void AppearanceAssociation::Refine(Costs& costs,
                                   const std::vector<STrack*>& tracks,
                                   const std::vector<std::size_t>& indices) {
  if (tracks.empty() || indices.empty()) {
    return;
  }
  if (costs.size() != tracks.size() ||
      std::any_of(costs.begin(), costs.end(), [&indices](const auto& row) {
        return row.size() != indices.size();
      })) {
    throw std::runtime_error("ByteTrack association matrix shape mismatch");
  }
  const auto appearance = BuildCosts(tracks, indices, costs);
  for (std::size_t row = 0; row < tracks.size(); ++row) {
    ApplyRow(indices, appearance, row, *tracks[row], costs[row]);
  }
}

void AppearanceAssociation::Measured(const STrack& track, std::size_t index) {
  const auto& measured = Measurement(index);
  measurements_.insert_or_assign(track.track_id, measured.box);
  auto found = galleries_.find(track.track_id);
  if (found != galleries_.end()) {
    if (stamp_ - found->second.seen > kContinuitySeconds &&
        !measured.appearance.empty()) {
      ++recoveries_;
    }
    found->second.seen = stamp_;
  }
  if (found != galleries_.end() && measured.ground_point &&
      measured.score >= kMinimumScore) {
    found->second.ground.Observe(*measured.ground_point, stamp_, retention_);
  }
  if (measured.appearance.empty() || measured.score < kMinimumScore) {
    return;
  }
  auto& gallery = galleries_[track.track_id];
  if (measured.ground_point) {
    gallery.ground.Observe(*measured.ground_point, stamp_, retention_);
  }
  gallery.seen = stamp_;
  gallery.observed_box = measured.box;
  if (!gallery.descriptors.empty() &&
      stamp_ - gallery.updated < kGalleryInterval) {
    return;
  }
  gallery.descriptors.push_back(measured.appearance);
  if (gallery.descriptors.size() > kGallerySize) {
    gallery.descriptors.pop_front();
  }
  gallery.updated = stamp_;
}

std::function<bool(std::size_t)> AppearanceAssociation::Initialization() {
  return [this](const std::size_t index) {
    if (deferred_.count(index) == 0) {
      return true;
    }
    ++deferred_creations_;
    return false;
  };
}

std::function<void(STrack&, std::size_t)> AppearanceAssociation::Recovery()
    const {
  if (!ground_motion_) {
    return {};
  }
  return [this](auto& track, auto index) { Recover(track, index); };
}

void AppearanceAssociation::Recover(STrack& track, std::size_t index) const {
  const auto& measured = Measurement(index);
  if (!ground_motion_ || !measured.ground_point ||
      galleries_.count(track.track_id) == 0) {
    return;
  }
  DETECTBOX box;
  box << static_cast<float>(measured.box.center_x),
      static_cast<float>(measured.box.center_y),
      static_cast<float>(measured.box.width / measured.box.height),
      static_cast<float>(measured.box.height);
  byte_kalman::KalmanFilter filter;
  const auto state = filter.initiate(box);
  // Verified ground/appearance association restores the measured image box.
  // An occlusion-corrupted image velocity must not drag the output off it.
  track.mean = state.first;
  track.covariance = state.second;
}

std::optional<Box> AppearanceAssociation::MeasuredBox(int identity) const {
  const auto found = measurements_.find(identity);
  return found == measurements_.end() ? std::nullopt
                                      : std::optional<Box>{found->second};
}

void AppearanceAssociation::Prune(const std::vector<int>& retained) {
  for (auto entry = galleries_.begin(); entry != galleries_.end();) {
    if (std::find(retained.begin(), retained.end(), entry->first) ==
        retained.end()) {
      entry = galleries_.erase(entry);
    } else {
      ++entry;
    }
  }
}
}  // namespace raven::tracking
