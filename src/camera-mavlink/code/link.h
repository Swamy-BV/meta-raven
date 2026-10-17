// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <mavlink/v2.0/common/mavlink.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

// The generated dialect owns message definitions and serialization helpers.

namespace raven::camera {
constexpr auto kDefaultRouterPort = 5760;

struct Endpoint {
  std::uint8_t system = 1;
  std::uint8_t component = MAV_COMP_ID_CAMERA;
  std::uint16_t port = kDefaultRouterPort;
};

// Single executor thread owns socket, parser and sequence state. TCP is local
// to mavlink-router; external telemetry transports remain router configuration.
class Link final {
 public:
  explicit Link(const Endpoint& endpoint);
  ~Link();
  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;
  Link(Link&&) = delete;
  Link& operator=(Link&&) = delete;

  void Tick(const std::function<void(const mavlink_message_t&)>& receive);
  [[nodiscard]] bool Connected() const;

  template <class Packet, class Encoder>
  void Send(const Packet& packet, const Encoder& encoder) {
    if (!Connected()) {
      return;
    }
    mavlink_message_t message{};
    encoder(system_, component_, &tx_status_, &message, &packet);
    Queue(message);
  }

 private:
  void Connect();
  void Disconnect();
  void Queue(const mavlink_message_t& message);
  void Flush();
  void Receive(const std::function<void(const mavlink_message_t&)>& receive);
  void Parse(const std::uint8_t* bytes, std::size_t count,
             const std::function<void(const mavlink_message_t&)>& receive);
  std::uint8_t system_;
  std::uint8_t component_;
  std::uint16_t port_;
  int socket_ = -1;
  bool connecting_ = false;
  std::chrono::steady_clock::time_point retry_;
  mavlink_status_t tx_status_{};
  mavlink_status_t rx_status_{};
  mavlink_message_t rx_buffer_{};
  std::deque<std::vector<std::uint8_t>> outgoing_;
  std::size_t offset_ = 0;
};
}  // namespace raven::camera
