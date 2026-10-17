// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "../../ports/media_store.h"

namespace raven::media {
class PhotoStore final : public MediaStore {
 public:
  PhotoStore(const std::string& root, int max_photos);
  [[nodiscard]] std::uint32_t Count() const override;
  [[nodiscard]] std::uint64_t FreeBytes() const override;
  [[nodiscard]] std::uint64_t TotalBytes() const override;
  Photo Write(const raven::contracts::CameraFrame& frame, int quality) override;

 private:
  std::filesystem::path root_;
  int max_photos_;
  std::uint32_t index_ = 0;
};
}  // namespace raven::media
