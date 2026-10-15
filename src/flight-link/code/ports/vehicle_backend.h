// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/vehicle_state.h"

namespace raven::flight {
// Read-only backend. Poll reports unknown values until current telemetry
// arrives.
class VehicleBackend {
 public:
  VehicleBackend() = default;
  virtual ~VehicleBackend() = default;
  VehicleBackend(const VehicleBackend&) = delete;
  VehicleBackend& operator=(const VehicleBackend&) = delete;
  VehicleBackend(VehicleBackend&&) = delete;
  VehicleBackend& operator=(VehicleBackend&&) = delete;
  [[nodiscard]] virtual Snapshot Poll() = 0;
};
}  // namespace raven::flight
