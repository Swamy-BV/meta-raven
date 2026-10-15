// SPDX-License-Identifier: AGPL-3.0-only
#include "vehicle_state.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace raven::flight {
namespace {
constexpr auto kMaxModeBytes = 32;

bool Fresh(Clock::time_point received, Clock::time_point now,
           std::chrono::milliseconds max_age) {
  return max_age.count() > 0 && received <= now && now - received < max_age;
}
}  // namespace

std::uint64_t StateStore::Attach(std::uint32_t system_id) {
  if (system_id == 0) {
    throw std::invalid_argument("autopilot system ID must be nonzero");
  }
  ++state_.epoch;
  state_.system_id = system_id;
  state_.connected = true;
  state_.armed.reset();
  state_.mode.reset();
  return state_.epoch;
}

void StateStore::Disconnect() {
  state_.connected = false;
  state_.armed.reset();
  state_.mode.reset();
}

void StateStore::UpdateArmed(std::uint64_t epoch, bool armed,
                             Clock::time_point received) {
  if (state_.connected && epoch == state_.epoch) {
    state_.armed = armed;
    armed_received_ = received;
  }
}

void StateStore::UpdateMode(std::uint64_t epoch, std::string mode,
                            Clock::time_point received) {
  if (!state_.connected || epoch != state_.epoch) {
    return;
  }
  if (mode.empty() || mode.size() > kMaxModeBytes) {
    state_.mode.reset();
    return;
  }
  state_.mode = std::move(mode);
  mode_received_ = received;
}

Snapshot StateStore::Read(Clock::time_point now,
                          std::chrono::milliseconds max_age) const {
  auto result = state_;
  if (!Fresh(armed_received_, now, max_age)) {
    result.armed.reset();
  }
  if (!Fresh(mode_received_, now, max_age)) {
    result.mode.reset();
  }
  return result;
}
}  // namespace raven::flight
