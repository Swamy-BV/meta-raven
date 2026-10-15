// SPDX-License-Identifier: AGPL-3.0-only
#include "pipeline_graph.h"

#include <filesystem>
#include <stdexcept>
#include <string>

#include <glib.h>
#include <gst/gst.h>

namespace fs = std::filesystem;

namespace {
constexpr auto kBitsPerKilobit = 1000;

std::string GstQuote(const std::string& value) {
  std::string result = "\"";
  for (char const ch : value) {
    if (ch == '\\' || ch == '"') {
      result += '\\';
    }
    result += ch;
  }
  return result + "\"";
}

class PipelineBuilder {
 public:
  explicit PipelineBuilder(const Config& config) : config_(config) {}

  [[nodiscard]] std::string Build() const { return PipelineGraph(); }

 private:
  [[nodiscard]] std::string RtpSourceGraph() const {
    // RTP time is reconstructed on the receiver clock, not sensor exposure
    // time.
    return "udpsrc address=" + GstQuote(config_.rtp_address) +
           " port=" + std::to_string(config_.rtp_port) +
           " buffer-size=262144 caps=" +
           GstQuote(
               "application/"
               "x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload="
               "96") +
           " ! rtpjitterbuffer latency=100 drop-on-latency=true ! "
           "rtph264depay wait-for-keyframe=true ! h264parse";
  }

  [[nodiscard]] std::string SourceGraph() const {
    std::string source;
    if (config_.source == "rtp") {
      return RtpSourceGraph() + " ! v4l2h264dec capture-io-mode=" +
             std::string(config_.converter == "g2d" ? "dmabuf" : "mmap");
    }
    if (config_.source == "test") {
      source = "videotestsrc is-live=true pattern=ball";
    } else if (config_.source == "libcamera") {
      source = "libcamerasrc" +
               (config_.camera_name.empty()
                    ? std::string()
                    : " camera-name=" + GstQuote(config_.camera_name));
    } else if (config_.source == "camera") {
      source = "v4l2src do-timestamp=true device=" + GstQuote(config_.device) +
               " ! decodebin";
    } else if (config_.codec == "h264") {
      // Follow NXP's explicit hardware graph. decodebin's exposed DMA_DRM
      // caps do not negotiate correctly through this BSP's downstream tee.
      source =
          "filesrc location=" + GstQuote(config_.input) +
          " ! qtdemux name=demux demux. ! video/x-h264 ! queue ! h264parse ! "
          "v4l2h264dec capture-io-mode=" +
          std::string(config_.converter == "g2d" ? "dmabuf" : "mmap");
    } else {
      gchar* uri = g_filename_to_uri(fs::absolute(config_.input).c_str(),
                                     nullptr, nullptr);
      if (!uri) {
        throw std::runtime_error("cannot create input-file URI");
      }
      source = "uridecodebin caps=" + GstQuote("video/x-raw(ANY)") +
               " uri=" + GstQuote(uri);
      g_free(uri);
    }
    return source;
  }

  [[nodiscard]] std::string EncoderGraph() const {
    std::string const encoder =
        config_.codec == "h264"
            ? "v4l2h264enc extra-controls=" +
                  GstQuote("encode,video_bitrate=" +
                           std::to_string(config_.bitrate * kBitsPerKilobit)) +
                  " ! h264parse"
            : "vp8enc deadline=1 cpu-used=8 threads=2 "
              "lag-in-frames=0 target-bitrate=" +
                  std::to_string(config_.bitrate * kBitsPerKilobit) +
                  " keyframe-max-dist=" + std::to_string(config_.fps);
    return encoder;
  }

  [[nodiscard]] std::string InferenceGraph() const {
    std::string const conversion = config_.converter == "g2d"
                                       ? "imxvideoconvert_g2d"
                                       : "videoconvert ! videoscale";
    std::string const inference =
        " ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
        "videorate drop-only=true ! video/x-raw(ANY),framerate=" +
        std::to_string(config_.fps) + "/1 ! " + conversion +
        " ! video/x-raw,format=" + (config_.codec == "vp8" ? "I420" : "NV12") +
        ",width=" + std::to_string(config_.width) +
        ",height=" + std::to_string(config_.height) +
        " ! identity sync=true ! tee name=raw "
        "raw. ! queue leaky=downstream max-size-buffers=2 max-size-bytes=0 "
        "max-size-time=0 ! " +
        std::string(config_.converter == "g2d" ? "imxvideoconvert_g2d"
                                               : "videoconvert") +
        " ! video/x-raw,format=RGB ! appsink name=photos emit-signals=true "
        "max-buffers=1 drop=true sync=false processing-deadline=0 ";
    return inference;
  }

  [[nodiscard]] std::string RecordingGraph() const {
    return " encoded. ! queue max-size-buffers=3 max-size-bytes=0 "
           "max-size-time=0 ! "
           "splitmuxsink muxer-factory=" +
           std::string(config_.codec != "vp8" ? "matroskamux" : "webmmux") +
           " async-finalize=true location=" +
           GstQuote(
               (fs::path(config_.output) / "recordings" /
                (config_.codec != "vp8" ? "clip-%05d.mkv" : "clip-%05d.webm"))
                   .string()) +
           " max-size-time=" +
           std::to_string(static_cast<guint64>(config_.segment) * GST_SECOND) +
           " max-size-bytes=10485760 max-files=" +
           std::to_string(config_.segments);
  }

  [[nodiscard]] std::string PipelineGraph() const {
    const auto source = SourceGraph();
    const auto encoder = EncoderGraph();
    const auto inference = InferenceGraph();
    std::string const stream =
        " encoded. ! queue max-size-buffers=3 max-size-bytes=0 "
        "max-size-time=0 "
        "! " +
        std::string(config_.codec != "vp8"
                        ? "rtph264pay config-interval=1 pt=96"
                        : "rtpvp8pay pt=96") +
        " ! udpsink sync=false async=false host=" + GstQuote(config_.host) +
        " port=" + std::to_string(config_.port);
    const auto output = config_.stream ? stream : "";
    std::string chain;
    if (config_.codec == "h264-copy") {
      const auto compressed =
          config_.source == "rtp"
              ? RtpSourceGraph()
              : "filesrc location=" + GstQuote(config_.input) +
                    " ! qtdemux name=demux demux. ! video/x-h264";
      chain = compressed +
              " ! queue "
              "max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! "
              "h264parse ! identity sync=true ! tee name=encoded "
              "encoded. ! queue max-size-buffers=3 max-size-bytes=0 "
              "max-size-time=0 ! "
              "h264parse ! v4l2h264dec capture-io-mode=" +
              std::string(config_.converter == "g2d" ? "dmabuf" : "mmap") +
              inference + output;
    } else {
      chain = source + inference;
      if (config_.stream || config_.recording) {
        chain +=
            "raw. ! queue max-size-buffers=3 max-size-bytes=0 "
            "max-size-time=0 ! " +
            encoder + " ! tee name=encoded" + output;
      }
    }
    if (config_.recording) {
      chain += RecordingGraph();
    }
    return chain;
  }

  const Config& config_;
};
}  // namespace

std::string BuildPipelineGraph(const Config& config) {
  return PipelineBuilder(config).Build();
}
