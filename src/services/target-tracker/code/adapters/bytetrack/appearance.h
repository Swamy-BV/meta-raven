// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <BYTETracker.h>
#include <STrack.h>

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "core/ground_motion.h"
#include "core/tracker.h"

namespace raven::tracking {
// Appearance supplements bounded motion association; it never emits
// predictions. The observations and input-index storage must outlive the
// synchronous ByteTrack update.
class AppearanceAssociation final {
 public:
  explicit AppearanceAssociation(bool ground_motion = false,
                                 double retention = kDefaultLostSeconds);
  AssociationHooks Bind(const std::vector<Observation>& observations,
                        const std::vector<std::size_t>& indices, double stamp);
  [[nodiscard]] std::function<void(STrack&, std::size_t)> Recovery() const;
  [[nodiscard]] std::function<bool(std::size_t)> Initialization();
  void Prune(const std::vector<int>& retained);
  // Current update only; no predicted or stale box is returned.
  [[nodiscard]] std::optional<Box> MeasuredBox(int identity) const;

  [[nodiscard]] std::size_t Deferred() const { return deferred_creations_; }

  [[nodiscard]] std::size_t Recoveries() const { return recoveries_; }

  [[nodiscard]] std::size_t Rejections() const { return rejections_; }

 private:
  struct Gallery {
    std::deque<std::vector<float>> descriptors;
    double updated = 0;
    double seen = 0;
    Box observed_box;
    GroundMotion ground;
  };

  using Costs = std::vector<std::vector<float>>;
  [[nodiscard]] bool Grounded(const Gallery& gallery,
                              const Observation& observation) const;

  struct Candidate {
    std::size_t input_index = 0;
    float iou_distance = 0;
  };

  [[nodiscard]] float AssociationCost(const STrack& track,
                                      const Candidate& candidate);
  [[nodiscard]] const Observation& Measurement(std::size_t index) const;
  void Refine(Costs& costs, const std::vector<STrack*>& tracks,
              const std::vector<std::size_t>& indices);
  void Measured(const STrack& track, std::size_t index);
  void Recover(STrack& track, std::size_t index) const;
  [[nodiscard]] Costs BuildCosts(const std::vector<STrack*>& tracks,
                                 const std::vector<std::size_t>& indices,
                                 const Costs& proximity);
  [[nodiscard]] float CandidateDistance(const STrack& track,
                                        const Observation& observation) const;
  [[nodiscard]] bool RecoveryAllows(const STrack& track, const Box& box,
                                    float proximity) const;
  void ApplyRow(const std::vector<std::size_t>& indices,
                const Costs& appearance, std::size_t row, const STrack& track,
                std::vector<float>& costs);
  [[nodiscard]] float Distance(const Gallery& gallery,
                               const std::vector<float>& descriptor) const;
  // Input indices deferred for this update only; no synthetic observation.
  std::set<std::size_t> deferred_;
  std::size_t deferred_creations_ = 0;
  std::map<int, Gallery> galleries_;
  std::map<int, Box> measurements_;
  const std::vector<Observation>* observations_ = nullptr;
  const std::vector<std::size_t>* indices_ = nullptr;
  double stamp_ = 0;
  bool ground_motion_ = false;
  double retention_ = kDefaultLostSeconds;
  std::size_t recoveries_ = 0;
  std::size_t rejections_ = 0;
};
}  // namespace raven::tracking
