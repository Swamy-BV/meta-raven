// SPDX-License-Identifier: AGPL-3.0-only

#include "session.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include "../core/types.h"
#include "../ports/capture_backend.h"
#include "../ports/media_store.h"

namespace raven::media {
namespace {
constexpr auto kMinimumFreeBytes = 64ULL * 1024 * 1024;
constexpr auto kPhotoTimeout = std::chrono::seconds(5);
constexpr auto kMaximumInterval = 3600.0;
constexpr auto kMaxRequestId = 128U;
constexpr auto kMinimumInterval = 0.1;
constexpr auto kMaximumBurst = 10000U;

std::string RecordingId() {
  const auto value = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  return "recording-" + std::to_string(value);
}
}  // namespace

Session::Session(Settings settings, CaptureBackend& backend, MediaStore& store)
    : settings_(std::move(settings)), backend_(backend), store_(store) {
  Validate(settings_);
  state_.image_count = store_.Count();
}

void Session::Start() { Reconfigure(settings_); }

void Session::Stop() {
  CancelCapture();
  backend_.Stop();
  state_.ready = false;
}

void Session::Reconfigure(Settings settings) {
  Validate(settings);
  if (state_.capturing) {
    throw std::runtime_error("capture sequence is busy");
  }
  if (settings.recording && store_.FreeBytes() < kMinimumFreeBytes) {
    throw std::runtime_error("insufficient recording storage");
  }
  state_.ready = false;
  try {
    backend_.Stop();
  } catch (const std::exception& error) {
    settings_.recording = false;
    settings_.streaming = false;
    state_.error = error.what();
    throw;
  }
  settings_ = std::move(settings);
  state_.recording_id = settings_.recording ? RecordingId() : std::string{};
  try {
    backend_.Start(settings_, state_.recording_id);
    if (controls_ && settings_.source.kind == "libcamera") {
      backend_.SetControls(*controls_);
    }
    state_.ready = true;
    state_.error.clear();
  } catch (const std::exception& error) {
    settings_.recording = false;
    settings_.streaming = false;
    state_.recording_id.clear();
    state_.error = error.what();
    throw;
  }
}

void Session::ConfigureProfile(const Profile& profile) {
  if (settings_.recording) {
    throw std::runtime_error("stop recording before changing profiles");
  }
  auto settings = settings_;
  SelectProfile(settings, profile.purpose) = profile;
  Reconfigure(std::move(settings));
}

void Session::ConfigureSource(const Source& source) {
  if (settings_.recording) {
    throw std::runtime_error("stop recording before changing sources");
  }
  auto settings = settings_;
  settings.source = source;
  Reconfigure(std::move(settings));
}

void Session::SetMode(const std::string& mode) {
  if (settings_.recording) {
    throw std::runtime_error("stop recording before changing mode");
  }
  auto settings = settings_;
  settings.mode = mode;
  Reconfigure(std::move(settings));
}

void Session::SetOutput(const std::string& output, bool enabled) {
  auto settings = settings_;
  if (output == "streaming") {
    if (settings.streaming == enabled) {
      return;
    }
    settings.streaming = enabled;
    Validate(settings);
    if (settings_.recording) {
      backend_.SetStreaming(enabled);
      settings_ = std::move(settings);
      return;
    }
  } else if (output == "recording") {
    if (settings.recording == enabled) {
      return;
    }
    settings.recording = enabled;
  } else {
    throw std::invalid_argument("output must be recording or streaming");
  }
  Reconfigure(std::move(settings));
}

void Session::SetControls(const Controls& controls) {
  backend_.SetControls(controls);
  controls_ = controls;
}

void Session::Capture(const std::string& request_id, std::uint32_t count,
                      double interval) {
  if (!state_.ready || state_.capturing) {
    throw std::runtime_error("capture session is unavailable or busy");
  }
  if (request_id.empty() || request_id.size() > kMaxRequestId ||
      count > kMaximumBurst || !std::isfinite(interval) || interval < 0 ||
      interval > kMaximumInterval ||
      (count != 1 && interval < kMinimumInterval)) {
    throw std::invalid_argument("invalid capture sequence");
  }
  request_id_ = request_id;
  remaining_ = count;
  infinite_ = count == 0;
  interval_ = interval;
  completed_ = 0;
  last_uri_.clear();
  state_.error.clear();
  state_.capturing = true;
  pending_ = false;
  next_ = std::chrono::steady_clock::now();
}

void Session::CancelCapture() {
  backend_.CancelPhoto();
  state_.capturing = false;
  pending_ = false;
}

void Session::CompletePhoto(const raven::contracts::CameraFrame& frame) {
  auto photo = store_.Write(frame, settings_.photo.quality);
  photo.request_id = request_id_;
  last_uri_ = photo.uri;
  state_.image_count = photo.index;
  event_ = std::move(photo);
  ++completed_;
  pending_ = false;
  if (!infinite_ && --remaining_ == 0) {
    state_.capturing = false;
  }
  next_ = std::chrono::steady_clock::now() +
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(interval_));
}

void Session::CaptureTick() {
  if (!state_.capturing) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!pending_ && now >= next_) {
    backend_.RequestPhoto();
    pending_ = true;
    deadline_ = now + kPhotoTimeout;
  }
  if (pending_) {
    if (auto frame = backend_.TakePhoto()) {
      CompletePhoto(*frame);
    } else if (now >= deadline_) {
      throw std::runtime_error("timed out waiting for a fresh photo frame");
    }
  }
}

void Session::Tick() {
  try {
    backend_.Poll();
    if (settings_.recording && store_.FreeBytes() < kMinimumFreeBytes) {
      throw std::runtime_error("recording stopped: storage reserve reached");
    }
    CaptureTick();
  } catch (const std::exception& error) {
    Fail(error.what());
  }
}

void Session::Fail(const std::string& reason) {
  state_.error = reason;
  CancelCapture();
  state_.ready = false;
  try {
    backend_.Stop();
  } catch (const std::exception& stop_error) {
    state_.error += std::string("; ") + stop_error.what();
  }
  settings_.recording = false;
  settings_.streaming = false;
}

std::optional<Photo> Session::TakeEvent() {
  auto event = std::move(event_);
  event_.reset();
  return event;
}

const Settings& Session::Configuration() const { return settings_; }

const State& Session::Status() const { return state_; }

std::uint32_t Session::Completed() const { return completed_; }

const std::string& Session::LastUri() const { return last_uri_; }

std::uint64_t Session::FreeBytes() const { return store_.FreeBytes(); }
}  // namespace raven::media
