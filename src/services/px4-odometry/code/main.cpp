// SPDX-License-Identifier: AGPL-3.0-only
#include <cstdio>
#include <exception>
#include <memory>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "node.h"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    rclcpp::spin(std::make_shared<raven::odometry::OdometryNode>());
  } catch (const std::exception& error) {
    std::fprintf(stderr, "px4-odometry: %s\n", error.what());
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
