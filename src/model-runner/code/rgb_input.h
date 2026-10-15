// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>

// For the validated YOLOv4 contract, round((byte/255)/(1/255))-128
// is exactly byte-128. Its int8 storage representation is byte XOR 0x80.
namespace {
constexpr auto kMaxRgbDimension = 4096;
}  // namespace

struct RgbLayout {
  const uint8_t* source = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
  int channels = 0;
};

inline void PackRgbRow(const uint8_t* row, uint8_t* out,
                       const RgbLayout& layout, uint8_t mask) {
  const auto width = layout.width;
  const auto channels = layout.channels;
  if (channels == 3) {
    if (!mask) {
      std::memcpy(out, row, static_cast<size_t>(width) * 3);
      return;
    }
    for (int i = 0; i < width * 3; ++i) {
      out[i] = row[i] ^ mask;
    }
    return;
  }
  for (int x = 0; x < width; ++x) {
    for (int c = 0; c < 3; ++c) {
      out[x * 3 + c] = row[x * channels + c] ^ mask;
    }
  }
}

inline void PackRgbTensor(const RgbLayout& layout, uint8_t* target,
                          bool signed_normalized) {
  const auto* source = layout.source;
  const auto width = layout.width;
  const auto height = layout.height;
  const auto stride = layout.stride;
  const auto channels = layout.channels;
  if (!source || !target || width < 1 || width > kMaxRgbDimension ||
      height < 1 || height > kMaxRgbDimension ||
      (channels != 3 && channels != 4) || stride < width * channels) {
    throw std::runtime_error("invalid RGB tensor packing geometry");
  }
  const uint8_t mask = signed_normalized ? 0x80 : 0;
  for (int y = 0; y < height; ++y) {
    const auto* row = source + static_cast<size_t>(y) * stride;
    auto* out = target + static_cast<size_t>(y) * width * 3;
    PackRgbRow(row, out, layout, mask);
  }
}
