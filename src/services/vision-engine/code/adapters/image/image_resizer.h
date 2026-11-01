// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "../../rgb_input.h"

namespace raven::vision {
[[nodiscard]] bool IsResizeBackend(std::string_view backend);

// Serial worker adapter. Resize completes before returning, including GPU
// readback. The result borrows input or adapter storage until the next call;
// callers must retain the input and pack the result before another Resize.
class ImageResizer {
 public:
  explicit ImageResizer(const std::string& backend);
  ~ImageResizer();
  ImageResizer(const ImageResizer&) = delete;
  ImageResizer& operator=(const ImageResizer&) = delete;
  ImageResizer(ImageResizer&&) = delete;
  ImageResizer& operator=(ImageResizer&&) = delete;
  [[nodiscard]] RgbLayout Resize(const RgbLayout& input, int width, int height);
  [[nodiscard]] const std::string& Backend() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace raven::vision
