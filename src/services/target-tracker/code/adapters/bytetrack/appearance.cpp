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
#include <stdexcept>
#include <utility>
#include <vector>

#include <Eigen/Core>

#include "core/tracker.h"

namespace raven::tracking {
namespace {
constexpr float kMaximumDistance = 0.3F;
constexpr float kAmbiguityMargin = 0.05F;
constexpr float kRejected = 1.0F;
constexpr float kRecoveryCost = 0.6F;
constexpr float kMaximumRecoveryIouDistance = 0.8F;
constexpr double kMinimumRecoveryOverlap =
    1.0 - static_cast<double>(kMaximumRecoveryIouDistance);
constexpr double kGalleryInterval = 0.5;
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

AssociationHooks AppearanceAssociation::Bind(
    const std::vector<Observation>& observations,
    const std::vector<std::size_t>& indices, double stamp) {
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
  if (found == galleries_.end() || observation.appearance.empty() ||
      !MotionAllows(track, observation.box)) {
    return kRejected;
  }
  return Distance(found->second, observation.appearance);
}

bool AppearanceAssociation::RecoveryAllows(const STrack& track, const Box& box,
                                           float proximity) const {
  if (track.state != TrackState::Lost ||
      proximity <= kMaximumRecoveryIouDistance) {
    return true;
  }
  // Partial occlusion can corrupt image velocity. Check the last confident
  // observation as well as the prediction, while retaining the motion gate.
  const auto found = galleries_.find(track.track_id);
  return found != galleries_.end() &&
         Overlap(found->second.observed_box, box) >= kMinimumRecoveryOverlap;
}

AppearanceAssociation::Costs AppearanceAssociation::BuildCosts(
    const std::vector<STrack*>& tracks, const std::vector<std::size_t>& indices,
    const Costs& proximity) const {
  Costs result;
  result.reserve(tracks.size());
  for (const auto* track : tracks) {
    std::vector<float> row;
    row.reserve(indices.size());
    for (const auto index : indices) {
      const auto& measured = Measurement(index);
      // Appearance cannot pull a lost identity onto a neighbouring lookalike
      // while the original vehicle remains occluded.
      const auto proximity_cost = proximity[result.size()][row.size()];
      row.push_back(RecoveryAllows(*track, measured.box, proximity_cost)
                        ? CandidateDistance(*track, measured)
                        : kRejected);
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
    if (distance > kMaximumDistance ||
        (recovering && !Unique(appearance, row, column))) {
      costs[column] = kRejected;
      ++rejections_;
    } else if (recovering) {
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
  auto found = galleries_.find(track.track_id);
  if (found != galleries_.end()) {
    if (stamp_ - found->second.seen > kContinuitySeconds &&
        !measured.appearance.empty()) {
      ++recoveries_;
    }
    found->second.seen = stamp_;
  }
  if (measured.appearance.empty() || measured.score < kMinimumScore) {
    return;
  }
  auto& gallery = galleries_[track.track_id];
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
