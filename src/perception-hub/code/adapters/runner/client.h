// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../../core/result.h"
#include "../../ports/model_backend.h"

namespace raven::perception {
class RunnerBackend final : public ModelBackend {
 public:
  RunnerBackend(std::string socket, std::string assets, bool automatic,
                std::uint64_t max_age_us);
  Snapshot Poll() override;

 private:
  [[nodiscard]] nlohmann::json Request(const std::string& command) const;
  void UpdateLabels(const nlohmann::json& status, const Identity& identity);
  std::string socket_, assets_, metadata_key_;
  std::vector<std::string> labels_;
  bool automatic_;
  std::uint64_t max_age_us_;
};
}  // namespace raven::perception
