// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <string>

#include "../../ports/capture_backend.h"

namespace raven::media {
class GstreamerBackend final : public CaptureBackend {
 public:
  GstreamerBackend();
  ~GstreamerBackend() override;
  GstreamerBackend(const GstreamerBackend&) = delete;
  GstreamerBackend& operator=(const GstreamerBackend&) = delete;
  GstreamerBackend(GstreamerBackend&&) = delete;
  GstreamerBackend& operator=(GstreamerBackend&&) = delete;
  void Start(const Settings& settings,
             const std::string& recording_id) override;
  void Stop() override;
  void Poll() override;
  void RequestPhoto() override;
  void CancelPhoto() override;
  void SetStreaming(bool enabled) override;
  [[nodiscard]] std::optional<raven::contracts::CameraFrame> TakePhoto()
      override;
  [[nodiscard]] std::optional<raven::contracts::CameraFrame> TakeFrame()
      override;
  void PushFrame(const raven::contracts::CameraFrame& frame) override;
  void SetControls(const Controls& controls) override;
  [[nodiscard]] std::vector<std::string> ControlNames() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace raven::media
