// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <string>

#include <raven/contracts/camera.h>

namespace raven::media {
// Called on the capture application's event-loop thread. Implementations must
// bound their queues; they never retain references to the supplied frame.
class FrameOutput {
 public:
  FrameOutput() = default;
  virtual ~FrameOutput() = default;
  FrameOutput(const FrameOutput&) = delete;
  FrameOutput& operator=(const FrameOutput&) = delete;
  FrameOutput(FrameOutput&&) = delete;
  FrameOutput& operator=(FrameOutput&&) = delete;
  virtual void BindSnapshot(std::function<std::string()> capture) = 0;
  virtual void Spin() = 0;
  [[nodiscard]] virtual bool Running() const = 0;
  [[nodiscard]] virtual bool WantsFrames() const = 0;
  virtual void Publish(raven::contracts::CameraFrame frame) = 0;
};
}  // namespace raven::media
