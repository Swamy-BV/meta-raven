// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/tracker.h"

namespace raven::tracking {
struct RgbFrame {
  const std::uint8_t* data = nullptr;
  std::size_t bytes = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
};

// Owns one fixed PP-LCNet vehicle network. Serial calls, no CPU graph fallback;
// frame memory is borrowed only during Encode. Resizing and normalization are
// explicit host preprocessing; all learned layers execute on Neutron.
class VehicleEncoder final {
 public:
  explicit VehicleEncoder(const std::string& model);
  ~VehicleEncoder();
  VehicleEncoder(const VehicleEncoder&) = delete;
  VehicleEncoder& operator=(const VehicleEncoder&) = delete;
  VehicleEncoder(VehicleEncoder&&) = delete;
  VehicleEncoder& operator=(VehicleEncoder&&) = delete;
  std::vector<float> Encode(const RgbFrame& frame, const Box& box);
  [[nodiscard]] double LastMilliseconds() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace raven::tracking
