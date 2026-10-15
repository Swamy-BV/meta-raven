// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/timer.hpp>

#include "../../ports/vehicle_backend.h"
#include "raven_interfaces/msg/vehicle_state.hpp"

namespace raven::flight {
// Publishes read-only state. No command services or action servers are
// installed.
using BackendFactory = std::function<std::unique_ptr<VehicleBackend>(
    const std::string&, std::chrono::milliseconds)>;

class FlightNode final : public rclcpp::Node {
 public:
  explicit FlightNode(const BackendFactory& create_backend);

 private:
  void Publish();
  std::unique_ptr<VehicleBackend> backend_;
  rclcpp::Publisher<raven_interfaces::msg::VehicleState>::SharedPtr
      state_publisher_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
      health_publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};
}  // namespace raven::flight
