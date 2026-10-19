// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../core/types.h"
#include <raven/contracts/camera.h>

namespace raven::media {
// All calls are serialized on the application thread. The backend owns its
// pipeline and retained hardware buffers; returned frames own copied pixels.
class CaptureBackend {
 public:
  CaptureBackend() = default;
  virtual ~CaptureBackend() = default;
  CaptureBackend(const CaptureBackend&) = delete;
  CaptureBackend& operator=(const CaptureBackend&) = delete;
  CaptureBackend(CaptureBackend&&) = delete;
  CaptureBackend& operator=(CaptureBackend&&) = delete;
  virtual void Start(const Settings& settings,
                     const std::string& recording_id) = 0;
  virtual void Stop() = 0;
  virtual void Poll() = 0;
  virtual void RequestPhoto() = 0;
  virtual void CancelPhoto() = 0;
  virtual void SetStreaming(bool enabled) = 0;
  [[nodiscard]] virtual std::optional<raven::contracts::CameraFrame>
  TakePhoto() = 0;
  [[nodiscard]] virtual std::optional<raven::contracts::CameraFrame>
  TakeFrame() = 0;
  virtual void PushFrame(const raven::contracts::CameraFrame& frame) = 0;
  virtual void SetControls(const Controls& controls) = 0;
  [[nodiscard]] virtual std::vector<std::string> ControlNames() const = 0;
};
}  // namespace raven::media
