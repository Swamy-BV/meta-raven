// SPDX-License-Identifier: AGPL-3.0-only

#include <exception>
#include <iostream>
#include <memory>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "node.h"

int main(int argc, char** argv) {
  try {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<raven::camera::CameraNode>());
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "camera-mavlink: " << error.what() << '\n';
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
}
