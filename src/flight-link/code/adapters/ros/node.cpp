// SPDX-License-Identifier: AGPL-3.0-only
#include "node.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>

#include "../../core/vehicle_state.h"
#include "../../ports/vehicle_backend.h"
#include "raven_interfaces/msg/vehicle_state.hpp"

namespace raven::flight {
namespace {
constexpr auto kPublishIntervalMs = 200;
constexpr auto kMaxTelemetryAgeMs = 60000;
constexpr auto kDefaultTelemetryAgeMs = 3000;

std::chrono::milliseconds TelemetryAge(rclcpp::Node& node) {
  const auto age = node.declare_parameter<std::int64_t>("telemetry_max_age_ms",
                                                        kDefaultTelemetryAgeMs);
  if (age < kPublishIntervalMs || age > kMaxTelemetryAgeMs) {
    throw std::invalid_argument("telemetry age outside supported bounds");
  }
  return std::chrono::milliseconds(age);
}

diagnostic_msgs::msg::DiagnosticStatus Health(const Snapshot& state) {
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = "flight-link/telemetry";
  status.hardware_id = std::to_string(state.system_id);
  if (!state.connected) {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    status.message = "PX4 disconnected; no command authority";
  } else if (!state.armed || !state.mode) {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
    status.message = "Telemetry incomplete or expired";
  } else {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
    status.message =
        "Read-only telemetry available; flight readiness not assessed";
  }
  return status;
}
}  // namespace

FlightNode::FlightNode(const BackendFactory& create_backend)
    : Node("flight_link", "/raven/vehicle_1/flight") {
  const auto url = declare_parameter<std::string>(
      "connection_url", "serial:///dev/ttyproxy:921600");
  backend_ = create_backend(url, TelemetryAge(*this));
  const auto qos = rclcpp::QoS(1).reliable().transient_local();
  state_publisher_ =
      create_publisher<raven_interfaces::msg::VehicleState>("state", qos);
  health_publisher_ =
      create_publisher<diagnostic_msgs::msg::DiagnosticArray>("health", qos);
  timer_ = create_wall_timer(std::chrono::milliseconds(kPublishIntervalMs),
                             [this] { Publish(); });
}

void FlightNode::Publish() {
  const auto state = backend_->Poll();
  raven_interfaces::msg::VehicleState message;
  message.stamp = now();
  message.connection_epoch = state.epoch;
  message.system_id = state.system_id;
  message.connected = state.connected;
  message.armed_valid = state.armed.has_value();
  message.armed = state.armed.value_or(false);
  message.mode_valid = state.mode.has_value();
  message.mode = state.mode.value_or("");
  state_publisher_->publish(message);

  diagnostic_msgs::msg::DiagnosticArray health;
  health.header.stamp = message.stamp;
  health.status.push_back(Health(state));
  health_publisher_->publish(health);
}
}  // namespace raven::flight
