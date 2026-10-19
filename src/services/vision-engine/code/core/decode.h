// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>

#include <nlohmann/json.hpp>

#include "result.h"

namespace raven::vision {
// Decode only live, current-generation, fresh results. Malformed input throws.
std::optional<Result> Decode(const nlohmann::json& status,
                             std::uint64_t now_monotonic_us,
                             std::uint64_t max_age_us);
// A failed selection requires an explicit operator retry; do not resume the old
// model.
bool MayInfer(const nlohmann::json& status);
Identity ActiveIdentity(const nlohmann::json& status);
}  // namespace raven::vision
