// SPDX-License-Identifier: AGPL-3.0-only

#include "link.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

namespace raven::camera {
namespace {
constexpr auto kReconnectDelay = std::chrono::seconds(2);
constexpr auto kQueueLimit = 64U;
constexpr auto kReceiveBytes = 4096U;
constexpr auto kReceiveBatches = 8;
}  // namespace

Link::Link(const Endpoint& endpoint)
    : system_(endpoint.system),
      component_(endpoint.component),
      port_(endpoint.port) {}

Link::~Link() { Disconnect(); }

bool Link::Connected() const { return socket_ >= 0 && !connecting_; }

void Link::Disconnect() {
  if (socket_ >= 0) {
    close(socket_);
  }
  socket_ = -1;
  connecting_ = false;
  outgoing_.clear();
  offset_ = 0;
  rx_status_ = {};
  rx_buffer_ = {};
  retry_ = std::chrono::steady_clock::now() + kReconnectDelay;
}

void Link::Connect() {
  if (std::chrono::steady_clock::now() < retry_) {
    return;
  }
  const auto flags = static_cast<int>(static_cast<unsigned>(SOCK_STREAM) |
                                      static_cast<unsigned>(SOCK_NONBLOCK) |
                                      static_cast<unsigned>(SOCK_CLOEXEC));
  socket_ = socket(AF_INET, flags, 0);
  if (socket_ < 0) {
    Disconnect();
    return;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port_);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  // POSIX sockaddr_in is passed through the socket API's generic address type.
  if (connect(socket_, reinterpret_cast<const sockaddr*>(&address),
              sizeof(address)) == 0) {
    connecting_ = false;
    return;
  }
  if (errno == EINPROGRESS) {
    connecting_ = true;
    return;
  }
  Disconnect();
}

void Link::Queue(const mavlink_message_t& message) {
  if (outgoing_.size() >= kQueueLimit) {
    // Disconnect rather than send a partial MAVLink frame after queue loss.
    Disconnect();
    return;
  }
  std::array<std::uint8_t, MAVLINK_MAX_PACKET_LEN> bytes{};
  const auto count = mavlink_msg_to_send_buffer(bytes.data(), &message);
  outgoing_.emplace_back(bytes.begin(), bytes.begin() + count);
}

void Link::Flush() {
  while (!outgoing_.empty()) {
    const auto& bytes = outgoing_.front();
    const auto count = send(socket_, bytes.data() + offset_,
                            bytes.size() - offset_, MSG_NOSIGNAL);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      Disconnect();
      return;
    }
    offset_ += static_cast<std::size_t>(count);
    if (offset_ == bytes.size()) {
      outgoing_.pop_front();
      offset_ = 0;
    }
  }
}

void Link::Receive(
    const std::function<void(const mavlink_message_t&)>& receive) {
  std::array<std::uint8_t, kReceiveBytes> bytes{};
  for (int batch = 0; batch < kReceiveBatches; ++batch) {
    const auto count = recv(socket_, bytes.data(), bytes.size(), 0);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      Disconnect();
      return;
    }
    Parse(bytes.data(), static_cast<std::size_t>(count), receive);
  }
}

void Link::Parse(const std::uint8_t* bytes, std::size_t count,
                 const std::function<void(const mavlink_message_t&)>& receive) {
  for (std::size_t index = 0; index < count; ++index) {
    mavlink_message_t message{};
    mavlink_status_t status{};
    if (mavlink_frame_char_buffer(&rx_buffer_, &rx_status_, bytes[index],
                                  &message, &status) == MAVLINK_FRAMING_OK) {
      receive(message);
    }
  }
}

void Link::Tick(const std::function<void(const mavlink_message_t&)>& receive) {
  if (socket_ < 0) {
    Connect();
  }
  if (connecting_) {
    pollfd descriptor{socket_, POLLOUT, 0};
    if (poll(&descriptor, 1, 0) <= 0) {
      return;
    }
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &length) != 0 ||
        error) {
      Disconnect();
      return;
    }
    connecting_ = false;
  }
  if (Connected()) {
    Flush();
  }
  if (Connected()) {
    Receive(receive);
  }
}
}  // namespace raven::camera
