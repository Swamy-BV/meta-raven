// SPDX-License-Identifier: AGPL-3.0-only
#include "backend.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <mavsdk/component_type.hpp>
#include <mavsdk/connection_result.hpp>
#include <mavsdk/mavsdk.hpp>
#include <mavsdk/plugins/telemetry/telemetry.hpp>
#include <mavsdk/system.hpp>

#include "../../core/vehicle_state.h"

namespace raven::flight {
struct SharedState {
  std::mutex mutex;
  StateStore store;
};

MavsdkBackend::MavsdkBackend(const std::string& url,
                             std::chrono::milliseconds max_age)
    : sdk_(mavsdk::Mavsdk::Configuration{
          mavsdk::ComponentType::CompanionComputer}),
      state_(std::make_shared<SharedState>()),
      max_age_(max_age) {
  if (max_age.count() <= 0) {
    throw std::invalid_argument("telemetry age must be positive");
  }
  if (sdk_.add_any_connection(url) != mavsdk::ConnectionResult::Success) {
    throw std::runtime_error("cannot open configured MAVLink transport");
  }
}

MavsdkBackend::~MavsdkBackend() { Drop(); }

void MavsdkBackend::Bind(const std::shared_ptr<mavsdk::System>& system) {
  system_ = system;
  telemetry_ = std::make_unique<mavsdk::Telemetry>(system_);
  std::uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> const lock(state_->mutex);
    epoch = state_->store.Attach(system_->get_system_id());
  }
  const std::weak_ptr<SharedState> weak = state_;
  armed_handle_ = telemetry_->subscribe_armed([weak, epoch](bool armed) {
    if (const auto state = weak.lock()) {
      std::lock_guard<std::mutex> const lock(state->mutex);
      state->store.UpdateArmed(epoch, armed, Clock::now());
    }
  });
  mode_handle_ = telemetry_->subscribe_flight_mode(
      [weak, epoch](mavsdk::Telemetry::FlightMode mode) {
        const auto state = weak.lock();
        if (!state) {
          return;
        }
        std::ostringstream text;
        if (mode != mavsdk::Telemetry::FlightMode::Unknown) {
          text << mode;
        }
        std::lock_guard<std::mutex> const lock(state->mutex);
        state->store.UpdateMode(epoch, text.str(), Clock::now());
      });
}

void MavsdkBackend::Drop() {
  // Invalidate first: callbacks already queued by MAVSDK must not restore
  // state.
  {
    std::lock_guard<std::mutex> const lock(state_->mutex);
    state_->store.Disconnect();
  }
  if (telemetry_) {
    telemetry_->unsubscribe_armed(armed_handle_);
    telemetry_->unsubscribe_flight_mode(mode_handle_);
    telemetry_.reset();
  }
  system_.reset();
}

void MavsdkBackend::Discover() {
  const auto systems = sdk_.systems();
  const auto found =
      std::find_if(systems.begin(), systems.end(),
                   [](const std::shared_ptr<mavsdk::System>& system) {
                     return system->has_autopilot() && system->is_connected();
                   });
  if (found != systems.end()) {
    Bind(*found);
  }
}

Snapshot MavsdkBackend::Poll() {
  if (system_ && !system_->is_connected()) {
    Drop();
  }
  if (!system_) {
    Discover();
  }
  std::lock_guard<std::mutex> const lock(state_->mutex);
  return state_->store.Read(Clock::now(), max_age_);
}
}  // namespace raven::flight
