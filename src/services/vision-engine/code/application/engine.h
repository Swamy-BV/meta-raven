// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/result.h"
#include <raven/contracts/camera.h>

namespace raven::vision {
// The provider returns an owned, latest frame on the application thread.
using FrameProvider = std::function<raven::contracts::CameraFrame()>;

class Engine final {
 public:
  Engine(const std::string& config, FrameProvider frames,
         std::uint64_t max_age_us);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;
  [[nodiscard]] std::vector<std::string> Models() const;
  [[nodiscard]] std::string InitialModel(const std::string& fallback);
  // Selection validates asynchronously; the active identity changes only on
  // success. An empty ID unloads the model. Busy requests throw without
  // changing selection.
  void Select(const std::string& id);
  void Cancel();
  // False means no newer source frame is available; no job is scheduled.
  [[nodiscard]] bool Infer();
  // Caller serializes model operations and polling on the ROS executor thread.
  [[nodiscard]] Snapshot Poll(bool automatic);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace raven::vision
