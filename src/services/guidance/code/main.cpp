// SPDX-License-Identifier: AGPL-3.0-only
#include <exception>
#include <iostream>
#include <memory>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/ros/node.h"

int main(int argc, char** argv) {
  try {
    rclcpp::init(argc, argv);
    {
      const auto node = std::make_shared<raven::guidance::GuidanceNode>();
      rclcpp::spin(node);
    }
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "guidance: " << error.what() << '\n';
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
}
