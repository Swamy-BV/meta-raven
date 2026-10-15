// SPDX-License-Identifier: AGPL-3.0-only
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/ros/node.h"
#include "adapters/runner/client.h"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    const raven::perception::BackendFactory factory =
        [](const std::string& socket, const std::string& assets, bool automatic,
           std::uint64_t age_us) {
          return std::make_unique<raven::perception::RunnerBackend>(
              socket, assets, automatic, age_us);
        };
    rclcpp::spin(std::make_shared<raven::perception::PerceptionNode>(factory));
  } catch (const std::exception& error) {
    std::cerr << "perception: " << error.what() << '\n';
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
