// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>

#include "../core/types.h"
#include <raven/contracts/camera.h>

namespace raven::media {
class MediaStore {
 public:
  MediaStore() = default;
  virtual ~MediaStore() = default;
  MediaStore(const MediaStore&) = delete;
  MediaStore& operator=(const MediaStore&) = delete;
  MediaStore(MediaStore&&) = delete;
  MediaStore& operator=(MediaStore&&) = delete;
  [[nodiscard]] virtual std::uint32_t Count() const = 0;
  [[nodiscard]] virtual std::uint64_t FreeBytes() const = 0;
  [[nodiscard]] virtual std::uint64_t TotalBytes() const = 0;
  virtual Photo Write(const raven::contracts::CameraFrame& frame,
                      int quality) = 0;
};
}  // namespace raven::media
