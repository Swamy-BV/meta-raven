// SPDX-License-Identifier: AGPL-3.0-only

#include "pipeline.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <glib.h>

#include "../../core/types.h"

namespace raven::media {
namespace {
constexpr auto kNanosecondsPerSecond = 1000000000ULL;
constexpr auto kBitsPerKbit = 1000;

std::string Quote(const std::string& text) {
  std::string result = "\"";
  for (const auto ch : text) {
    if (ch == '\\' || ch == '"') {
      result += '\\';
    }
    result += ch;
  }
  return result + "\"";
}

std::string RtpSource(const Source& source) {
  const std::string extension =
      source.rtp_capture_timestamps
          ? ",extmap-1=urn:ietf:params:rtp-hdrext:ntp-64"
          : "";
  return "udpsrc buffer-size=4194304 uri=" + Quote(source.uri) +
         " "
         "caps=\"application/x-rtp,media=video,encoding-name=H264,"
         "clock-rate=90000,payload=96" +
         extension +
         "\" ! rtpjitterbuffer latency=100 "
         "drop-on-latency=true ! rtph264depay ! h264parse ! "
         "v4l2h264dec capture-io-mode=dmabuf";
}

std::string SourceGraph(const Settings& settings) {
  const auto& source = settings.source;
  if (source.kind == "test") {
    return "videotestsrc is-live=true pattern=ball";
  }
  if (source.kind == "ros") {
    return "appsrc name=ros_input is-live=true format=time block=false "
           "max-buffers=2 leaky-type=downstream do-timestamp=true";
  }
  if (source.kind == "libcamera") {
    auto graph =
        std::string("libcamerasrc name=camera src::stream-role=") +
        (settings.mode == "photo" ? "still-capture" : "video-recording");
    if (!source.camera_name.empty()) {
      graph += " camera-name=" + Quote(source.camera_name);
    }
    graph += " ! video/x-raw,format=NV12";
    if (source.width > 0 && source.height > 0) {
      graph += ",width=" + std::to_string(source.width) +
               ",height=" + std::to_string(source.height);
    }
    return graph;
  }
  if (source.kind == "camera") {
    return "v4l2src do-timestamp=true device=" + Quote(source.uri) +
           " ! decodebin";
  }
  if (source.kind == "rtp") {
    return RtpSource(source);
  }
  if (source.kind == "image") {
    return "filesrc location=" + Quote(source.uri) +
           " ! decodebin ! imagefreeze is-live=true ! identity sync=true";
  }
  const auto absolute = std::filesystem::absolute(source.uri).string();
  gchar* uri = g_filename_to_uri(absolute.c_str(), nullptr, nullptr);
  if (!uri) {
    throw std::invalid_argument("invalid replay file");
  }
  // Stop automatic plugging at H.264 so the hardware decoder negotiates
  // directly with our processing branches, including their memory features.
  const std::string result = "uridecodebin name=replay uri=" + Quote(uri) +
                             " caps=video/x-h264 ! h264parse ! v4l2h264dec "
                             "capture-io-mode=dmabuf ! identity sync=true";
  g_free(uri);
  return result;
}

std::string Convert(const Settings& settings, const Profile& profile,
                    const std::string& format) {
  return std::string(settings.converter == "g2d"
                         ? "imxvideoconvert_g2d"
                         : "videoconvert ! videoscale") +
         " ! video/x-raw,format=" + format +
         ",width=" + std::to_string(profile.width) +
         ",height=" + std::to_string(profile.height);
}

std::string Queue() {
  return " ! queue leaky=downstream max-size-buffers=2 "
         "max-size-bytes=0 max-size-time=0 ! ";
}

std::string VideoGraph(const Settings& settings,
                       const std::string& recording_id) {
  if (!settings.streaming && !settings.recording) {
    return {};
  }
  const auto& profile = settings.recording ? settings.video : settings.preview;
  auto graph =
      " source." + Queue() +
      "videorate drop-only=true ! video/x-raw(ANY),framerate=" +
      std::to_string(profile.fps) + "/1 ! " +
      Convert(settings, profile, "NV12") + " ! " + settings.encoder +
      " extra-controls=" +
      Quote("encode,video_bitrate=" +
            std::to_string(profile.bitrate_kbps * kBitsPerKbit)) +
      " ! video/x-h264,profile=baseline ! h264parse ! "
      "video/x-h264,alignment=au ! tee name=encoded "
      "encoded. ! queue max-size-buffers=4 max-size-bytes=0 max-size-time=0 "
      "! " +
      "valve name=stream_gate drop=" + (settings.streaming ? "false" : "true") +
      " drop-mode=transform-to-gap ! rtph264pay config-interval=1 pt=96 ! "
      "udpsink sync=false async=false host=" +
      Quote(settings.stream_host) +
      " port=" + std::to_string(settings.stream_port);
  if (settings.recording) {
    graph +=
        " encoded. ! queue max-size-buffers=3 max-size-bytes=0 "
        "max-size-time=0 ! splitmuxsink name=recorder "
        "muxer-factory=matroskamux "
        "async-finalize=false location=" +
        Quote((std::filesystem::path(settings.output_dir) / "recordings" /
               (recording_id + "-%05d.mkv"))
                  .string()) +
        " max-size-time=" +
        std::to_string(static_cast<std::uint64_t>(settings.segment_seconds) *
                       kNanosecondsPerSecond);
  }
  return graph;
}
}  // namespace

std::string BuildPipeline(const Settings& settings,
                          const std::string& recording_id) {
  return SourceGraph(settings) +
         " ! tee name=source "
         "source." +
         Queue() + "videorate drop-only=true ! video/x-raw(ANY),framerate=" +
         std::to_string(settings.image.fps) + "/1 ! " +
         Convert(settings, settings.image, "RGB") +
         " ! appsink name=frames max-buffers=1 drop=true sync=false "
         "async=false "
         "wait-on-eos=false source." +
         Queue() +
         "valve name=photo_gate drop=true drop-mode=transform-to-gap ! " +
         Convert(settings, settings.photo, "RGB") +
         " ! appsink name=photos max-buffers=1 drop=true sync=false "
         "async=false "
         "wait-on-eos=false" +
         VideoGraph(settings, recording_id);
}
}  // namespace raven::media
