// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <rclcpp/node.hpp>

#include "../../media_config.h"

[[nodiscard]] Config ReadSettings(rclcpp::Node& node);
