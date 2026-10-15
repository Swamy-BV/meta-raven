// SPDX-License-Identifier: AGPL-3.0-only
#include "capture.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "../../media_config.h"
#include "../../pipeline_graph.h"
#include "../../ports/frame_output.h"
#include <raven/contracts/camera.h>

namespace fs = std::filesystem;

namespace {
constexpr auto kHardwareDecoderRankBoost = 1000;
constexpr auto kFrameFreshnessMicros = 2000000;
constexpr auto kMaintenanceIntervalSeconds = 5;
constexpr auto kChannelBits = 8;
constexpr auto kPhotoPermissions = 0600;
constexpr auto kMinimumFreeStorageBytes = 64ULL * 1024 * 1024;

void Fail(const std::string& message) { throw std::runtime_error(message); }

static void CheckPlugins(const std::vector<const char*>& names) {
  for (const char* name : names) {
    GstElementFactory* factory = gst_element_factory_find(name);
    if (!factory) {
      Fail(std::string("missing GStreamer plugin: ") + name);
    }
    // Prefer the packaged software VP8 decoder for the portable baseline.
    if (std::strcmp(name, "vp8dec") == 0) {
      // Keep the upstream checked GObject cast.
      // NOLINTNEXTLINE(bugprone-casting-through-void)
      gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(factory),
                                  GST_RANK_PRIMARY + kHardwareDecoderRankBoost);
    }
    gst_object_unref(factory);
  }
}

static const char* SourcePlugin(const Config& c) {
  if (c.source == "rtp") {
    return "udpsrc";
  }
  if (c.source == "test") {
    return "videotestsrc";
  }
  if (c.source == "file") {
    return "uridecodebin";
  }
  if (c.source == "libcamera") {
    return "libcamerasrc";
  }
  return "v4l2src";
}

static void OutputPlugins(const Config& c, std::vector<const char*>& names) {
  if (!c.stream && !c.recording) {
    return;
  }
  if (c.codec == "vp8") {
    names.insert(names.end(), {"vp8dec", "vp8enc", "rtpvp8pay"});
    return;
  }
  names.push_back(c.codec == "h264" ? "v4l2h264enc" : "v4l2h264dec");
  names.insert(names.end(), {"h264parse", "rtph264pay"});
}

static void Plugins(const Config& c) {
  std::vector<const char*> names = {"queue",     "videoconvert", "videoscale",
                                    "videorate", "identity",     "tee",
                                    "appsink"};
  if (c.stream) {
    names.push_back("udpsink");
  }
  OutputPlugins(c, names);
  if (c.converter == "g2d") {
    names.push_back("imxvideoconvert_g2d");
  }
  names.push_back(SourcePlugin(c));
  if (c.source == "rtp") {
    names.insert(names.end(),
                 {"udpsrc", "rtpjitterbuffer", "rtph264depay", "v4l2h264dec"});
  }
  if (c.source == "file" && c.codec != "vp8") {
    names.push_back("qtdemux");
    names.push_back("v4l2h264dec");
  }
  if (c.recording) {
    names.push_back("splitmuxsink");
    names.push_back(c.codec != "vp8" ? "matroskamux" : "webmmux");
  }
  CheckPlugins(names);
}

static void DecoderAdded(GstBin*, GstBin*, GstElement* element, gpointer data) {
  GstElementFactory const* factory = gst_element_get_factory(element);
  if (!factory) {
    return;
  }
  std::string const name =
      // Keep the upstream checked GObject cast.
      // NOLINTNEXTLINE(bugprone-casting-through-void)
      gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
  if (name.rfind("v4l2", 0) == 0 && name.size() >= 3 &&
      name.compare(name.size() - 3, 3, "dec") == 0 &&
      g_object_class_find_property(G_OBJECT_GET_CLASS(element),
                                   "capture-io-mode")) {
    const auto& config = *static_cast<const Config*>(data);
    // Keep the upstream checked cast; older GLib macros shift signed literals.
    // NOLINTNEXTLINE(bugprone-casting-through-void,hicpp-signed-bitwise)
    gst_util_set_object_arg(G_OBJECT(element), "capture-io-mode",
                            config.converter == "g2d" ? "dmabuf" : "mmap");
  }
}

class Capture {
 public:
  Capture(const Capture&) = delete;
  Capture& operator=(const Capture&) = delete;
  Capture(Capture&&) = delete;
  Capture& operator=(Capture&&) = delete;

  explicit Capture(Config config, raven::media::FrameOutput& output)
      : config_(std::move(config)), output_(output) {}

  ~Capture() {
    stopping_ = true;
    {
      std::lock_guard<std::mutex> const lock(mutex_);
      if (latest_) {
        gst_sample_unref(latest_);
      }
      latest_ = nullptr;
    }
    if (pipeline_) {
      gst_element_set_state(pipeline_, GST_STATE_NULL);
      gst_object_unref(pipeline_);
    }
    if (loop_) {
      g_main_loop_unref(loop_);
    }
  }

 private:
  void ApplyCaptureAge(uint64_t age_us) {
    source_time_mapped_ = age_us < captured_us_ && age_us < captured_unix_us_;
    if (source_time_mapped_) {
      captured_us_ -= age_us;
      captured_unix_us_ -= age_us;
    }
  }

  void UpdateCaptureTime(GstSample* s) {
    const GstSegment* segment = gst_sample_get_segment(s);
    GstClock* clock = gst_element_get_clock(pipeline_);
    if (clock && segment && pts_valid_) {
      auto running =
          gst_segment_to_running_time(segment, GST_FORMAT_TIME, source_pts_);
      auto now = gst_clock_get_time(clock);
      auto base = gst_element_get_base_time(pipeline_);
      if (GST_CLOCK_TIME_IS_VALID(running) && GST_CLOCK_TIME_IS_VALID(base) &&
          base <= now && running <= now - base) {
        auto age_us = (now - base - running) / GST_USECOND;
        ApplyCaptureAge(age_us);
      }
    }
    if (clock) {
      gst_object_unref(clock);
    }
  }

  static GstFlowReturn Sample(GstAppSink* sink, gpointer data) {
    auto& h = *static_cast<Capture*>(data);
    GstSample* s = gst_app_sink_pull_sample(sink);
    if (!s) {
      return GST_FLOW_EOS;
    }
    GstVideoInfo info{};
    if (!gst_video_info_from_caps(&info, gst_sample_get_caps(s)) ||
        GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_RGB ||
        GST_VIDEO_INFO_WIDTH(&info) != h.config_.width ||
        GST_VIDEO_INFO_HEIGHT(&info) != h.config_.height) {
      gst_sample_unref(s);
      return GST_FLOW_ERROR;
    }
    std::lock_guard<std::mutex> const lock(h.mutex_);
    if (h.stopping_) {
      gst_sample_unref(s);
      return GST_FLOW_OK;
    }
    if (h.latest_) {
      gst_sample_unref(h.latest_);
    }
    h.latest_ = s;
    GstBuffer* buffer = gst_sample_get_buffer(s);
    h.last_frame_ = g_get_monotonic_time();
    ++h.frames_;
    h.captured_us_ = static_cast<uint64_t>(h.last_frame_);
    h.captured_unix_us_ = static_cast<uint64_t>(g_get_real_time());
    h.source_time_mapped_ = false;
    h.pts_valid_ = GST_BUFFER_PTS_IS_VALID(buffer);
    h.source_pts_ = h.pts_valid_ ? GST_BUFFER_PTS(buffer) : 0;
    h.UpdateCaptureTime(s);
    return GST_FLOW_OK;
  }

  raven::contracts::CameraFrame CurrentFrame() {
    raven::contracts::CameraFrame frame;
    std::unique_ptr<GstSample, decltype(&gst_sample_unref)> held(
        nullptr, gst_sample_unref);
    {
      std::lock_guard<std::mutex> const lock(mutex_);
      if (!latest_ ||
          g_get_monotonic_time() - last_frame_ > kFrameFreshnessMicros) {
        Fail("no fresh video frame");
      }
      held.reset(gst_sample_ref(latest_));
      frame.sequence = frames_;
      frame.captured_us = captured_us_;
      frame.captured_unix_us = captured_unix_us_;
      frame.source_time_mapped = source_time_mapped_;
      frame.pts_ns = source_pts_;
      frame.pts_valid = pts_valid_;
      frame.width = config_.width;
      frame.height = config_.height;
    }
    // Keep one hardware buffer; map/copy it only when a consumer requests
    // pixels.
    size_t const row = static_cast<size_t>(frame.width) * 3;
    frame.data.resize(row * frame.height);
    GstVideoInfo info{};
    // Zero initialization is required before GStreamer maps this structure.
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    GstVideoFrame mapped{};
    if (!gst_video_info_from_caps(&info, gst_sample_get_caps(held.get())) ||
        !gst_video_frame_map(&mapped, &info, gst_sample_get_buffer(held.get()),
                             GST_MAP_READ)) {
      Fail("cannot map RGB frame");
    }
    auto* source =
        static_cast<const uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&mapped, 0));
    for (int y = 0; y < frame.height; ++y) {
      std::memcpy(frame.data.data() + y * row,
                  source + static_cast<ptrdiff_t>(y) *
                               GST_VIDEO_FRAME_PLANE_STRIDE(&mapped, 0),
                  row);
    }
    gst_video_frame_unmap(&mapped);
    return frame;
  }

  static gboolean PublishFrame(gpointer data) {
    auto& capture = *static_cast<Capture*>(data);
    try {
      capture.output_.Spin();
      if (!capture.output_.Running()) {
        g_main_loop_quit(capture.loop_);
        return G_SOURCE_REMOVE;
      }
      if (!capture.output_.WantsFrames() || !capture.HasNewFrame()) {
        return G_SOURCE_CONTINUE;
      }
      auto frame = capture.CurrentFrame();
      capture.published_sequence_ = frame.sequence;
      capture.output_.Publish(std::move(frame));
    } catch (const std::exception& error) {
      std::cerr << "media-capture: ROS publication failed: " << error.what()
                << '\n';
      capture.result_ = 1;
      g_main_loop_quit(capture.loop_);
      return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
  }

  bool HasNewFrame() {
    std::lock_guard<std::mutex> const lock(mutex_);
    return latest_ && frames_ != published_sequence_ &&
           g_get_monotonic_time() - last_frame_ <= kFrameFreshnessMicros;
  }

  static gboolean Bus(GstBus*, GstMessage* message, gpointer data) {
    auto& h = *static_cast<Capture*>(data);
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
      GError* error = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &error, &debug);
      std::cerr << "media-capture: " << error->message << '\n';
      if (debug) {
        std::cerr << debug << '\n';
      }
      g_clear_error(&error);
      g_free(debug);
      h.result_ = 1;
      g_main_loop_quit(h.loop_);
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
      std::cout << "media-capture: end of stream; recordings finalized\n";
      g_main_loop_quit(h.loop_);
    }
    return G_SOURCE_CONTINUE;
  }

  static gboolean Timeout(gpointer data) {
    auto& h = *static_cast<Capture*>(data);
    std::cerr
        << "media-capture: EOS timeout; final recording may be incomplete\n";
    h.result_ = 1;
    g_main_loop_quit(h.loop_);
    return G_SOURCE_REMOVE;
  }

  static gboolean Stop(gpointer data) {
    auto& h = *static_cast<Capture*>(data);
    if (!h.stopping_) {
      h.stopping_ = true;
      gst_element_send_event(h.pipeline_, gst_event_new_eos());
      g_timeout_add_seconds(kMaintenanceIntervalSeconds, Timeout, &h);
    }
    return G_SOURCE_CONTINUE;
  }

  void CheckSnapshotCapacity() const {
    if (stopping_) {
      Fail("service is stopping");
    }
    size_t count = 0;
    for (const auto& entry :
         fs::directory_iterator(fs::path(config_.output) / "photos")) {
      if (entry.path().extension() == ".jpg") {
        ++count;
      }
    }
    if (count >= static_cast<size_t>(config_.snapshots)) {
      Fail("snapshot limit reached; archive or remove old photos");
    }
    if (fs::space(config_.output).available < kMinimumFreeStorageBytes) {
      Fail("less than 64 MiB free storage");
    }
  }

  void EncodeSnapshot(std::vector<uint8_t>& image) const {
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(
        image.data(), GDK_COLORSPACE_RGB, false, kChannelBits, config_.width,
        config_.height, config_.width * 3, nullptr, nullptr);
    if (!pixbuf) {
      Fail("cannot wrap snapshot pixels");
    }
    gchar* encoded = nullptr;
    gsize length = 0;
    GError* error = nullptr;
    bool const saved = gdk_pixbuf_save_to_buffer(
        pixbuf, &encoded, &length, "jpeg", &error, "quality", "90", nullptr);
    g_object_unref(pixbuf);
    if (!saved) {
      std::string const message =
          error ? error->message : "snapshot encoding failed";
      g_clear_error(&error);
      g_free(encoded);
      Fail(message);
    }
    image.assign(reinterpret_cast<uint8_t*>(encoded),
                 reinterpret_cast<uint8_t*>(encoded) + length);
    g_free(encoded);
  }

  std::string Snapshot() {
    CheckSnapshotCapacity();
    auto frame = CurrentFrame();
    auto image = std::move(frame.data);
    EncodeSnapshot(image);
    std::string const path =
        (fs::path(config_.output) / "photos" /
         ("photo-" + std::to_string(g_get_real_time()) + ".jpg"))
            .string();
    int const fd = open(path.c_str(),
                        static_cast<int>(static_cast<unsigned>(O_WRONLY) |
                                         static_cast<unsigned>(O_CREAT) |
                                         static_cast<unsigned>(O_EXCL) |
                                         static_cast<unsigned>(O_NOFOLLOW) |
                                         static_cast<unsigned>(O_CLOEXEC)),
                        kPhotoPermissions);
    if (fd < 0) {
      Fail("cannot create photo");
    }
    size_t written = 0;
    while (written < image.size()) {
      ssize_t const n =
          write(fd, image.data() + written, image.size() - written);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        close(fd);
        unlink(path.c_str());
        Fail("photo write failed");
      }
      written += static_cast<size_t>(n);
    }
    int const synced = fsync(fd);
    int const closed = close(fd);
    if (synced || closed) {
      unlink(path.c_str());
      Fail("photo sync failed");
    }
    return path;
  }

 public:
  int Run() {
    fs::create_directories(fs::path(config_.output) / "photos");
    fs::create_directories(fs::path(config_.output) / "recordings");
    if (config_.recording &&
        fs::space(config_.output).available < kMinimumFreeStorageBytes) {
      Fail("less than 64 MiB free storage");
    }
    output_.BindSnapshot([this] { return Snapshot(); });
    const auto chain = BuildPipelineGraph(config_);
    GError* error = nullptr;
    pipeline_ = gst_parse_launch(chain.c_str(), &error);
    if (error) {
      std::string const message = error->message;
      g_clear_error(&error);
      Fail(message);
    }
    if (!pipeline_) {
      Fail("cannot construct video pipeline");
    }
    g_signal_connect(pipeline_, "deep-element-added", G_CALLBACK(DecoderAdded),
                     &config_);
    loop_ = g_main_loop_new(nullptr, FALSE);
    // Keep the upstream checked GObject cast.
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    GstElement* photos = gst_bin_get_by_name(GST_BIN(pipeline_), "photos");
    g_signal_connect(photos, "new-sample", G_CALLBACK(Sample), this);
    gst_object_unref(photos);
    GstBus* messages = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(messages, Bus, this);
    gst_object_unref(messages);
    g_unix_signal_add(SIGTERM, Stop, this);
    g_unix_signal_add(SIGINT, Stop, this);
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
      Fail("cannot start video pipeline");
    }
    std::cout << "media-capture: ROS RGB source=" << config_.source << '\n';
    // Poll the retained latest sample on the event loop, never block the
    // GStreamer streaming thread or queue frames behind a slow ROS consumer.
    constexpr auto kPublicationPollMs = 10;
    const auto publish_source =
        g_timeout_add(kPublicationPollMs, PublishFrame, this);
    g_main_loop_run(loop_);
    if (g_main_context_find_source_by_id(nullptr, publish_source)) {
      g_source_remove(publish_source);
    }
    return result_;
  }

 private:
  Config config_;
  raven::media::FrameOutput& output_;
  std::uint64_t published_sequence_ = 0;
  GMainLoop* loop_ = nullptr;
  GstElement* pipeline_ = nullptr;
  std::atomic<bool> stopping_{false};
  int result_ = 0;
  std::mutex mutex_;
  GstSample* latest_ = nullptr;
  gint64 last_frame_ = 0;
  size_t frames_ = 0;
  uint64_t captured_us_ = 0, source_pts_ = 0, captured_unix_us_ = 0;
  bool source_time_mapped_ = false;
  bool pts_valid_ = false;
};

}  // namespace

int RunCapture(Config config, raven::media::FrameOutput& output) {
  gst_init(nullptr, nullptr);
  Plugins(config);
  Capture capture(std::move(config), output);
  return capture.Run();
}
