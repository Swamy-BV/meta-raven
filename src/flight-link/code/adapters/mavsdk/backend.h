// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <mavsdk/mavsdk.hpp>
#include <mavsdk/plugins/telemetry/telemetry.hpp>
#include <mavsdk/system.hpp>

#include "../../ports/vehicle_backend.h"

namespace raven::flight {
struct SharedState;

// Owns one MAVLink connection. Construction fails only for invalid transport
// setup.
class MavsdkBackend final : public VehicleBackend {
 public:
  explicit MavsdkBackend(const std::string& url,
                         std::chrono::milliseconds max_age);
  ~MavsdkBackend() override;
  MavsdkBackend(const MavsdkBackend&) = delete;
  MavsdkBackend& operator=(const MavsdkBackend&) = delete;
  MavsdkBackend(MavsdkBackend&&) = delete;
  MavsdkBackend& operator=(MavsdkBackend&&) = delete;
  [[nodiscard]] Snapshot Poll() override;

 private:
  void Bind(const std::shared_ptr<mavsdk::System>& system);
  void Drop();
  void Discover();
  mavsdk::Mavsdk sdk_;
  std::shared_ptr<SharedState> state_;
  std::shared_ptr<mavsdk::System> system_;
  std::unique_ptr<mavsdk::Telemetry> telemetry_;
  mavsdk::Telemetry::ArmedHandle armed_handle_;
  mavsdk::Telemetry::FlightModeHandle mode_handle_;
  std::chrono::milliseconds max_age_;
};
}  // namespace raven::flight
