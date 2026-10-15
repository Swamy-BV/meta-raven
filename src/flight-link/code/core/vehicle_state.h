// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace raven::flight {
using Clock = std::chrono::steady_clock;

struct Snapshot {
  std::uint64_t epoch = 0;
  std::uint32_t system_id = 0;
  bool connected = false;
  std::optional<bool> armed;
  std::optional<std::string> mode;
};

// Caller serializes access. Receipt freshness does not prove acquisition
// freshness.
class StateStore {
 public:
  // Starts a new attachment epoch and invalidates all previous telemetry.
  [[nodiscard]] std::uint64_t Attach(std::uint32_t system_id);
  void Disconnect();
  void UpdateArmed(std::uint64_t epoch, bool armed, Clock::time_point received);
  void UpdateMode(std::uint64_t epoch, std::string mode,
                  Clock::time_point received);
  [[nodiscard]] Snapshot Read(Clock::time_point now,
                              std::chrono::milliseconds max_age) const;

 private:
  Snapshot state_;
  Clock::time_point armed_received_;
  Clock::time_point mode_received_;
};
}  // namespace raven::flight
