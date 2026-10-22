// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <BYTETracker.h>
#include <STrack.h>

#include <cstddef>
#include <deque>
#include <map>
#include <vector>

#include "core/tracker.h"

namespace raven::tracking {
// Appearance supplements bounded motion association; it never emits
// predictions. The observations and input-index storage must outlive the
// synchronous ByteTrack update.
class AppearanceAssociation final {
 public:
  AssociationHooks Bind(const std::vector<Observation>& observations,
                        const std::vector<std::size_t>& indices, double stamp);
  void Prune(const std::vector<int>& retained);

  [[nodiscard]] std::size_t Recoveries() const { return recoveries_; }

  [[nodiscard]] std::size_t Rejections() const { return rejections_; }

 private:
  struct Gallery {
    std::deque<std::vector<float>> descriptors;
    double updated = 0;
    double seen = 0;
    Box observed_box;
  };

  using Costs = std::vector<std::vector<float>>;
  [[nodiscard]] const Observation& Measurement(std::size_t index) const;
  void Refine(Costs& costs, const std::vector<STrack*>& tracks,
              const std::vector<std::size_t>& indices);
  void Measured(const STrack& track, std::size_t index);
  [[nodiscard]] Costs BuildCosts(const std::vector<STrack*>& tracks,
                                 const std::vector<std::size_t>& indices,
                                 const Costs& proximity) const;
  [[nodiscard]] float CandidateDistance(const STrack& track,
                                        const Observation& observation) const;
  [[nodiscard]] bool RecoveryAllows(const STrack& track, const Box& box,
                                    float proximity) const;
  void ApplyRow(const std::vector<std::size_t>& indices,
                const Costs& appearance, std::size_t row, const STrack& track,
                std::vector<float>& costs);
  [[nodiscard]] float Distance(const Gallery& gallery,
                               const std::vector<float>& descriptor) const;
  std::map<int, Gallery> galleries_;
  const std::vector<Observation>* observations_ = nullptr;
  const std::vector<std::size_t>* indices_ = nullptr;
  double stamp_ = 0;
  std::size_t recoveries_ = 0;
  std::size_t rejections_ = 0;
};
}  // namespace raven::tracking
