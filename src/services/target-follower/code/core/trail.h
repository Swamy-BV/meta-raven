// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <deque>
#include <optional>

#include "follower.h"

namespace raven::following {
// A bounded trail of camera-derived local-NED target estimates. It supplies an
// arc-length-delayed reference; it does not plan around unobserved obstacles.
class TargetTrail {
 public:
  void Reset();
  void Observe(const Motion& motion);
  [[nodiscard]] std::optional<Motion> Reference(double distance) const;

 private:
  std::deque<Motion> points_;
};
}  // namespace raven::following
