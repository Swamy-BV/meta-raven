// SPDX-License-Identifier: AGPL-3.0-only
#include "worker_frame_buffer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <raven/contracts/camera.h>

namespace {
constexpr auto kBytes = raven::contracts::kMaxCameraBytes;
constexpr int kMinimumDimension = 64;
constexpr int kMaximumWidth = 1920;
constexpr int kMaximumHeight = 1080;
constexpr int kChannels = 3;
constexpr auto kSeals = static_cast<unsigned>(F_SEAL_GROW) |
                        static_cast<unsigned>(F_SEAL_SHRINK) |
                        static_cast<unsigned>(F_SEAL_FUTURE_WRITE) |
                        static_cast<unsigned>(F_SEAL_SEAL);

bool MappingFailed(const void* mapping) {
  // POSIX defines MAP_FAILED as an integer-to-pointer sentinel.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return mapping == MAP_FAILED;
}

void Require(const bool valid, const char* message) {
  if (!valid) {
    throw std::runtime_error(message);
  }
}

void ValidateDimensions(const int width, const int height) {
  Require(width >= kMinimumDimension && width <= kMaximumWidth &&
              height >= kMinimumDimension && height <= kMaximumHeight &&
              static_cast<std::size_t>(width) * height * kChannels <= kBytes,
          "invalid shared inference frame dimensions");
}
}  // namespace

WorkerFrameBuffer::WorkerFrameBuffer() {
  try {
    descriptor_ = memfd_create("vision-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    Require(descriptor_ >= 0 &&
                ftruncate(descriptor_, static_cast<off_t>(kBytes)) == 0,
            "cannot allocate inference frame buffer");
    mapping_ = mmap(nullptr, kBytes,
                    static_cast<int>(static_cast<unsigned>(PROT_READ) |
                                     static_cast<unsigned>(PROT_WRITE)),
                    MAP_SHARED, descriptor_, 0);
    Require(!MappingFailed(mapping_), "cannot map inference frame buffer");
    writable_ = static_cast<std::uint8_t*>(mapping_);
    // Existing parent mapping stays writable. The size and future writable
    // mappings are sealed before passing a read-only descriptor to the child.
    Require(fcntl(descriptor_, F_ADD_SEALS, static_cast<int>(kSeals)) == 0,
            "cannot seal inference frame buffer");
    const auto path = "/proc/self/fd/" + std::to_string(descriptor_);
    reader_ =
        open(path.c_str(), static_cast<int>(static_cast<unsigned>(O_RDONLY) |
                                            static_cast<unsigned>(O_CLOEXEC)));
    Require(reader_ >= 0, "cannot open read-only inference frame buffer");
  } catch (...) {
    Release();
    throw;
  }
}

WorkerFrameBuffer::WorkerFrameBuffer(const int inherited)
    : descriptor_(inherited) {
  try {
    struct stat status{};
    const auto seals = fcntl(descriptor_, F_GET_SEALS);
    Require(fstat(descriptor_, &status) == 0 &&
                status.st_size == static_cast<off_t>(kBytes) && seals >= 0 &&
                (static_cast<unsigned>(seals) & kSeals) == kSeals &&
                (static_cast<unsigned>(fcntl(descriptor_, F_GETFL)) &
                 static_cast<unsigned>(O_ACCMODE)) ==
                    static_cast<unsigned>(O_RDONLY),
            "invalid inherited inference frame buffer");
    Require(fcntl(descriptor_, F_SETFD, FD_CLOEXEC) == 0,
            "cannot protect inherited frame descriptor");
    mapping_ = mmap(nullptr, kBytes, PROT_READ, MAP_SHARED, descriptor_, 0);
    Require(!MappingFailed(mapping_), "cannot map worker frame buffer");
  } catch (...) {
    Release();
    throw;
  }
}

void WorkerFrameBuffer::Release() {
  if (mapping_ != nullptr && !MappingFailed(mapping_)) {
    munmap(mapping_, kBytes);
  }
  if (reader_ >= 0) {
    close(reader_);
  }
  if (descriptor_ >= 0) {
    close(descriptor_);
  }
  mapping_ = nullptr;
  writable_ = nullptr;
  descriptor_ = reader_ = -1;
}

WorkerFrameBuffer::~WorkerFrameBuffer() { Release(); }

void WorkerFrameBuffer::Write(const RgbLayout& image) {
  ValidateDimensions(image.width, image.height);
  Require(writable_ != nullptr && image.source != nullptr &&
              image.channels == kChannels &&
              image.stride >= image.width * kChannels,
          "invalid shared inference RGB layout");
  PackRgbTensor(image, writable_, false);
}

RgbLayout WorkerFrameBuffer::Read(const int width, const int height) const {
  ValidateDimensions(width, height);
  return {static_cast<const std::uint8_t*>(mapping_), width, height,
          width * kChannels, kChannels};
}
