// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

#include "media_config.h"

// Build the selected GStreamer graph from validated settings; no plugins are
// loaded. URI conversion errors are reported as exceptions. No backend fallback
// is performed.
[[nodiscard]] std::string BuildPipelineGraph(const Config& config);
