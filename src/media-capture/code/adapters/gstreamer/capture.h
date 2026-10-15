// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../../media_config.h"
#include "../../ports/frame_output.h"

// Runs capture and ROS callbacks on one event loop. The output must outlive
// this call; its snapshot callback borrows the capture backend only while it
// runs. GStreamer retains one latest frame and maps pixels on demand.
int RunCapture(Config config, raven::media::FrameOutput& output);
