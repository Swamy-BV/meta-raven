// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "../core/types.h"
#include "../ports/capture_backend.h"
#include "../ports/media_store.h"
#include <raven/contracts/camera.h>

namespace raven::media {
// The ROS executor and Tick serialize all application operations. Injected
// backend/store references must outlive this session.
class Session {
 public:
  Session(Settings settings, CaptureBackend& backend, MediaStore& store);
  void Start();
  void Stop();
  void Tick();
  void Fail(const std::string& reason);
  void ConfigureProfile(const Profile& profile);
  void ConfigureSource(const Source& source);
  void SetMode(const std::string& mode);
  void SetOutput(const std::string& output, bool enabled);
  void SetControls(const Controls& controls);
  void Capture(const std::string& request_id, std::uint32_t count,
               double interval);
  void CancelCapture();
  [[nodiscard]] std::optional<Photo> TakeEvent();
  [[nodiscard]] const Settings& Configuration() const;
  [[nodiscard]] const State& Status() const;
  [[nodiscard]] std::uint32_t Completed() const;
  [[nodiscard]] const std::string& LastUri() const;
  [[nodiscard]] std::uint64_t FreeBytes() const;

 private:
  void Reconfigure(Settings settings);
  void CaptureTick();
  void CompletePhoto(const raven::contracts::CameraFrame& frame);
  Settings settings_;
  CaptureBackend& backend_;
  MediaStore& store_;
  State state_;
  std::optional<Photo> event_;
  std::optional<Controls> controls_;
  std::string request_id_;
  std::string last_uri_;
  std::uint32_t remaining_ = 0;
  std::uint32_t completed_ = 0;
  double interval_ = 0;
  bool infinite_ = false;
  bool pending_ = false;
  std::chrono::steady_clock::time_point next_;
  std::chrono::steady_clock::time_point deadline_;
};
}  // namespace raven::media
