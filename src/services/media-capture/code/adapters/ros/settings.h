// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../../core/types.h"
#include "raven_interfaces/msg/media_profile.hpp"

namespace raven::media {
[[nodiscard]] Profile FromMessage(
    const raven_interfaces::msg::MediaProfile& message);
[[nodiscard]] raven_interfaces::msg::MediaProfile ToMessage(
    const Profile& profile);
}  // namespace raven::media
