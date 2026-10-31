// SPDX-License-Identifier: AGPL-3.0-only
#include <exception>
#include <iostream>
#include <memory>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/ros/node.h"

int main(const int argc, char** argv) {
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<raven::navigation::FramesNode>());
  } catch (const std::exception& error) {
    std::cerr << "navigation-frames: " << error.what() << '\n';
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
