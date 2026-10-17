// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

#include "../../core/types.h"

namespace raven::media {
[[nodiscard]] std::string BuildPipeline(const Settings& settings,
                                        const std::string& recording_id);
}  // namespace raven::media
