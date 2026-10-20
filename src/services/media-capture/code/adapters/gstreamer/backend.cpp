// SPDX-License-Identifier: AGPL-3.0-only

#include "backend.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <glib-object.h>
#include <glib.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "../../core/types.h"
#include "pipeline.h"
#include <raven/contracts/camera.h>

namespace raven::media {
namespace {
constexpr auto kChannels = 3;
constexpr auto kNanosecondsPerMicrosecond = 1000ULL;
constexpr auto kShutdownTimeout = 5 * GST_SECOND;
constexpr auto kMaxFrameBytes = 4096ULL * 3072 * kChannels;
using Sample = std::unique_ptr<GstSample, decltype(&gst_sample_unref)>;
using Caps = std::unique_ptr<GstCaps, decltype(&gst_caps_unref)>;
using Buffer = std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)>;
constexpr auto kRosTimestampReference = "timestamp/x-raven-ros";

void AttachOrigin(GstBuffer* buffer,
                  const raven::contracts::CameraFrame& frame) {
  const Caps reference(gst_caps_new_empty_simple(kRosTimestampReference),
                       gst_caps_unref);
  // GLib's public string-type macro shifts a signed literal.
  // NOLINTBEGIN(hicpp-signed-bitwise)
  gst_structure_set(gst_caps_get_structure(reference.get(), 0), "frame-id",
                    G_TYPE_STRING, frame.optical_frame.c_str(), nullptr);
  // NOLINTEND(hicpp-signed-bitwise)
  if (!gst_buffer_add_reference_timestamp_meta(buffer, reference.get(),
                                               frame.source_stamp_ns,
                                               GST_CLOCK_TIME_NONE)) {
    throw std::runtime_error("cannot attach ROS source identity");
  }
}

void RestoreOrigin(GstBuffer* buffer, raven::contracts::CameraFrame& frame) {
  const Caps reference(gst_caps_new_empty_simple(kRosTimestampReference),
                       gst_caps_unref);
  const auto* identity =
      gst_buffer_get_reference_timestamp_meta(buffer, reference.get());
  if (!identity) {
    throw std::runtime_error(
        "ROS source identity was lost by the processing pipeline");
  }
  const auto* optical_frame = gst_structure_get_string(
      gst_caps_get_structure(identity->reference, 0), "frame-id");
  if (!optical_frame) {
    throw std::runtime_error("ROS optical frame metadata is missing");
  }
  frame.source_stamp_ns = identity->timestamp;
  frame.captured_unix_us = identity->timestamp / kNanosecondsPerMicrosecond;
  frame.optical_frame = optical_frame;
}

void RestoreRtpOrigin(GstBuffer* buffer, raven::contracts::CameraFrame& frame) {
  constexpr auto kNtpUnixOffset = 2208988800ULL * GST_SECOND;
  const Caps reference(gst_caps_new_empty_simple("timestamp/x-ntp"),
                       gst_caps_unref);
  const auto* origin =
      gst_buffer_get_reference_timestamp_meta(buffer, reference.get());
  if (!origin || origin->timestamp <= kNtpUnixOffset) {
    throw std::runtime_error("RTP capture timestamp is missing or invalid");
  }
  // The sender and ROS nodes must share a clock domain. In simulation this is
  // simulated Unix-epoch time; physical senders require synchronized UTC
  // clocks.
  frame.source_stamp_ns = origin->timestamp - kNtpUnixOffset;
}

std::string MessageError(GstMessage* message) {
  GError* error = nullptr;
  gchar* debug = nullptr;
  gst_message_parse_error(message, &error, &debug);
  const std::string result = error ? error->message : "unknown GStreamer error";
  g_clear_error(&error);
  g_free(debug);
  return result;
}

void Timestamp(raven::contracts::CameraFrame& frame, GstBuffer* buffer,
               GstElement* pipeline) {
  frame.captured_us = static_cast<std::uint64_t>(g_get_monotonic_time());
  frame.captured_unix_us = static_cast<std::uint64_t>(g_get_real_time());
  frame.pts_valid = GST_BUFFER_PTS_IS_VALID(buffer);
  frame.pts_ns = frame.pts_valid ? GST_BUFFER_PTS(buffer) : 0;
  GstClock* clock = gst_element_get_clock(pipeline);
  if (!clock) {
    return;
  }
  const auto base = gst_element_get_base_time(pipeline);
  const auto now = gst_clock_get_time(clock);
  gst_object_unref(clock);
  if (!frame.pts_valid || base > now || frame.pts_ns > now - base) {
    return;
  }
  const auto age = (now - base - frame.pts_ns) / kNanosecondsPerMicrosecond;
  if (age < frame.captured_us && age < frame.captured_unix_us) {
    frame.captured_us -= age;
    frame.captured_unix_us -= age;
    frame.source_time_mapped = true;
  }
}

raven::contracts::CameraFrame Pixels(GstSample* sample, GstElement* pipeline) {
  GstVideoInfo info{};
  if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) ||
      GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_RGB) {
    throw std::runtime_error("expected RGB8 frame");
  }
  raven::contracts::CameraFrame frame;
  frame.width = GST_VIDEO_INFO_WIDTH(&info);
  frame.height = GST_VIDEO_INFO_HEIGHT(&info);
  const auto row = static_cast<std::size_t>(frame.width) * kChannels;
  if (frame.width <= 0 || frame.height <= 0 ||
      row * static_cast<std::size_t>(frame.height) > kMaxFrameBytes) {
    throw std::runtime_error("camera frame exceeds bounds");
  }
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  Timestamp(frame, buffer, pipeline);
  // GStreamer requires a zero-initialized mapping structure.
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
  GstVideoFrame mapped{};
  if (!gst_video_frame_map(&mapped, &info, buffer, GST_MAP_READ)) {
    throw std::runtime_error("cannot map camera frame");
  }
  frame.data.resize(row * frame.height);
  const auto* source =
      static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&mapped, 0));
  for (int y = 0; y < frame.height; ++y) {
    std::memcpy(frame.data.data() + y * row,
                source + static_cast<std::ptrdiff_t>(y) *
                             GST_VIDEO_FRAME_PLANE_STRIDE(&mapped, 0),
                row);
  }
  gst_video_frame_unmap(&mapped);
  return frame;
}

void Gate(GstElement* gate, bool closed) {
  if (gate) {
    g_object_set(gate, "drop", static_cast<gboolean>(closed), nullptr);
  }
}

GstElement* Element(GstElement* pipeline, const char* name) {
  // GstBin's checked cast is the public API for retrieving owned elements.
  // NOLINTNEXTLINE(bugprone-casting-through-void)
  return gst_bin_get_by_name(GST_BIN(pipeline), name);
}

}  // namespace

class GstreamerBackend::Impl {
 public:
  Settings settings;
  GstElement* pipeline = nullptr;
  GstElement* photos = nullptr;
  GstElement* frames = nullptr;
  GstElement* photo_gate = nullptr;
  GstElement* stream_gate = nullptr;
  GstElement* input = nullptr;
  GstBus* bus = nullptr;
  bool photo_pending = false;
  bool eos = false;
  std::uint64_t sequence = 0;

  void EndOfStream() {
    eos = true;
    if (!settings.source.loop || settings.source.kind != "file") {
      throw std::runtime_error("source reached end of stream");
    }
    const auto flags = static_cast<GstSeekFlags>(
        static_cast<unsigned>(GST_SEEK_FLAG_FLUSH) |
        static_cast<unsigned>(GST_SEEK_FLAG_KEY_UNIT));
    if (!gst_element_seek_simple(pipeline, GST_FORMAT_TIME, flags, 0)) {
      throw std::runtime_error("replay seek failed");
    }
    eos = false;
  }

  void Clear() {
    for (auto** element :
         {&photos, &frames, &photo_gate, &stream_gate, &input}) {
      if (*element) {
        gst_object_unref(*element);
        *element = nullptr;
      }
    }
    if (bus) {
      gst_object_unref(bus);
      bus = nullptr;
    }
    if (pipeline) {
      gst_object_unref(pipeline);
      pipeline = nullptr;
    }
    photo_pending = false;
    eos = false;
  }

  std::optional<raven::contracts::CameraFrame> Take(GstElement* sink) {
    if (!sink) {
      return std::nullopt;
    }
    // The named sinks are constructed as GstAppSink by our own graph.
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    const Sample sample(gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0),
                        gst_sample_unref);
    if (!sample) {
      return std::nullopt;
    }
    auto frame = Pixels(sample.get(), pipeline);
    frame.sequence = ++sequence;
    if (settings.source.kind == "ros") {
      RestoreOrigin(gst_sample_get_buffer(sample.get()), frame);
    } else if (settings.source.kind == "rtp" &&
               settings.source.rtp_capture_timestamps) {
      RestoreRtpOrigin(gst_sample_get_buffer(sample.get()), frame);
    }
    return frame;
  }
};

GstreamerBackend::GstreamerBackend() : impl_(std::make_unique<Impl>()) {
  gst_init(nullptr, nullptr);
}

GstreamerBackend::~GstreamerBackend() {
  if (impl_->pipeline) {
    gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
  }
  impl_->Clear();
}

void GstreamerBackend::Start(const Settings& settings,
                             const std::string& recording_id) {
  impl_->settings = settings;
  GError* error = nullptr;
  const auto graph = BuildPipeline(settings, recording_id);
  impl_->pipeline = gst_parse_launch(graph.c_str(), &error);
  if (error || !impl_->pipeline) {
    const std::string message =
        error ? error->message : "cannot construct pipeline";
    g_clear_error(&error);
    impl_->Clear();
    throw std::runtime_error(message);
  }
  impl_->photos = Element(impl_->pipeline, "photos");
  impl_->frames = Element(impl_->pipeline, "frames");
  impl_->photo_gate = Element(impl_->pipeline, "photo_gate");
  impl_->stream_gate = Element(impl_->pipeline, "stream_gate");
  impl_->input = Element(impl_->pipeline, "ros_input");
  impl_->bus = gst_element_get_bus(impl_->pipeline);
  if (gst_element_set_state(impl_->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    Stop();
    throw std::runtime_error("cannot start capture pipeline");
  }
}

void GstreamerBackend::Stop() {
  if (!impl_->pipeline) {
    return;
  }
  Gate(impl_->photo_gate, false);
  Gate(impl_->stream_gate, false);
  if (impl_->input) {
    // The graph creates this element as GstAppSrc.
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    gst_app_src_end_of_stream(GST_APP_SRC(impl_->input));
  } else {
    gst_element_send_event(impl_->pipeline, gst_event_new_eos());
  }
  const auto mask =
      static_cast<GstMessageType>(static_cast<unsigned>(GST_MESSAGE_EOS) |
                                  static_cast<unsigned>(GST_MESSAGE_ERROR));
  GstMessage* message = impl_->eos ? nullptr
                                   : gst_bus_timed_pop_filtered(
                                         impl_->bus, kShutdownTimeout, mask);
  const bool finalized =
      impl_->eos || (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS);
  if (message) {
    gst_message_unref(message);
  }
  gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
  const bool recording = impl_->settings.recording;
  impl_->Clear();
  if (recording && !finalized) {
    throw std::runtime_error("recording finalization failed or timed out");
  }
}

void GstreamerBackend::Poll() {
  if (!impl_->bus) {
    return;
  }
  while (auto* message = gst_bus_pop(impl_->bus)) {
    const auto type = GST_MESSAGE_TYPE(message);
    const auto error =
        type == GST_MESSAGE_ERROR ? MessageError(message) : std::string{};
    gst_message_unref(message);
    if (!error.empty()) {
      throw std::runtime_error(error);
    }
    if (type == GST_MESSAGE_EOS) {
      impl_->EndOfStream();
    }
  }
}

void GstreamerBackend::RequestPhoto() {
  while (impl_->Take(impl_->photos)) {
  }
  impl_->photo_pending = true;
  Gate(impl_->photo_gate, false);
}

void GstreamerBackend::CancelPhoto() {
  impl_->photo_pending = false;
  Gate(impl_->photo_gate, true);
}

void GstreamerBackend::SetStreaming(bool enabled) {
  if (!impl_->stream_gate) {
    throw std::runtime_error("video encoder is not active");
  }
  Gate(impl_->stream_gate, !enabled);
  impl_->settings.streaming = enabled;
}

std::optional<raven::contracts::CameraFrame> GstreamerBackend::TakePhoto() {
  if (!impl_->photo_pending) {
    return std::nullopt;
  }
  auto frame = impl_->Take(impl_->photos);
  if (frame) {
    CancelPhoto();
  }
  return frame;
}

std::optional<raven::contracts::CameraFrame> GstreamerBackend::TakeFrame() {
  return impl_->Take(impl_->frames);
}

void GstreamerBackend::PushFrame(const raven::contracts::CameraFrame& frame) {
  if (!impl_->input) {
    return;
  }
  // GLib's public fundamental-type macros shift signed literals.
  // NOLINTBEGIN(hicpp-signed-bitwise)
  GstCaps* caps = gst_caps_new_simple(
      "video/x-raw", "format", G_TYPE_STRING, "RGB", "width", G_TYPE_INT,
      frame.width, "height", G_TYPE_INT, frame.height, "framerate",
      GST_TYPE_FRACTION, impl_->settings.image.fps, 1, nullptr);
  // NOLINTEND(hicpp-signed-bitwise)
  // This element is always the graph's GstAppSrc.
  // NOLINTNEXTLINE(bugprone-casting-through-void)
  auto* source = GST_APP_SRC(impl_->input);
  gst_app_src_set_caps(source, caps);
  gst_caps_unref(caps);
  Buffer buffer(gst_buffer_new_allocate(nullptr, frame.data.size(), nullptr),
                gst_buffer_unref);
  if (!buffer) {
    throw std::runtime_error("cannot allocate ROS input frame");
  }
  gst_buffer_fill(buffer.get(), 0, frame.data.data(), frame.data.size());
  AttachOrigin(buffer.get(), frame);
  if (gst_app_src_push_buffer(source, buffer.release()) != GST_FLOW_OK) {
    throw std::runtime_error("ROS input pipeline rejected frame");
  }
}

void GstreamerBackend::SetControls(const Controls&) {
  // libcamerasrc exposes global properties even when the configured sensor
  // cannot apply them. Do not acknowledge controls without a supported native
  // control/readback adapter.
  throw std::runtime_error(
      "sensor controls are unavailable through this capture adapter");
}

std::vector<std::string> GstreamerBackend::ControlNames() const { return {}; }
}  // namespace raven::media
