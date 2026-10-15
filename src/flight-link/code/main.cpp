// SPDX-License-Identifier: AGPL-3.0-only
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/mavsdk/backend.h"
#include "adapters/ros/node.h"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    const raven::flight::BackendFactory factory =
        [](const std::string& url, std::chrono::milliseconds max_age) {
          return std::make_unique<raven::flight::MavsdkBackend>(url, max_age);
        };
    rclcpp::spin(std::make_shared<raven::flight::FlightNode>(factory));
  } catch (const std::exception& error) {
    std::cerr << "flight-link: " << error.what() << '\n';
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
