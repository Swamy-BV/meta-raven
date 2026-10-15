// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace localipc {
using Clock = std::chrono::steady_clock;

inline void Check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void SetSocketPath(sockaddr_un& address, const std::string& path) {
  Check(!path.empty() && path.size() < sizeof(address.sun_path) &&
            path.find('\0') == std::string::npos,
        "invalid Unix socket path");
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
}

// Buffers are borrowed; exactly one of source/destination is supplied.
struct TransferBuffer {
  const void* source = nullptr;
  void* destination = nullptr;
  size_t size = 0;
};

inline void Transfer(int fd, const TransferBuffer& buffer,
                     Clock::time_point deadline,
                     const std::atomic<bool>* cancel = nullptr) {
  const auto* source = buffer.source;
  auto* destination = buffer.destination;
  const auto size = buffer.size;
  Check((source == nullptr) != (destination == nullptr),
        "invalid transfer buffer");
  const bool writing = source != nullptr;
  const auto* outgoing = static_cast<const uint8_t*>(source);
  auto* incoming = static_cast<uint8_t*>(destination);
  size_t done = 0;
  while (done < size) {
    Check(!cancel || !cancel->load(), "job cancelled");
    Check(Clock::now() < deadline, "local IPC timeout");
    pollfd wait{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    int const polled = poll(&wait, 1, 5);
    if (polled < 0 && errno == EINTR) {
      continue;
    }
    Check(polled >= 0, "frame transport poll failed");
    if (!polled) {
      continue;
    }
    ssize_t const n =
        writing ? send(fd, outgoing + done, size - done,
                       MSG_NOSIGNAL | MSG_DONTWAIT)
                : recv(fd, incoming + done, size - done, MSG_DONTWAIT);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
      continue;
    }
    Check(n > 0, "frame transport disconnected");
    Check(static_cast<size_t>(n) <= size - done, "invalid transfer length");
    done += static_cast<size_t>(n);
  }
}

inline void SendBytes(int fd, const void* data, size_t size,
                      Clock::time_point deadline,
                      const std::atomic<bool>* cancel = nullptr) {
  Transfer(fd, {data, nullptr, size}, deadline, cancel);
}

inline void ReceiveBytes(int fd, void* data, size_t size,
                         Clock::time_point deadline,
                         const std::atomic<bool>* cancel = nullptr) {
  Transfer(fd, {nullptr, data, size}, deadline, cancel);
}

}  // namespace localipc
