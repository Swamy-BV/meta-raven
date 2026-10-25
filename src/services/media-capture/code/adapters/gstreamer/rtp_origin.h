// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>

#include <glib.h>
#include <gst/gst.h>

namespace raven::media {
// Preserve acquisition identity when a hardware encoder discards buffer
// metadata. The pipeline must be stopped before destruction removes its probes.
class RtpOrigin {
 public:
  explicit RtpOrigin(GstElement* pipeline);
  ~RtpOrigin();
  RtpOrigin(const RtpOrigin&) = delete;
  RtpOrigin& operator=(const RtpOrigin&) = delete;
  RtpOrigin(RtpOrigin&&) = delete;
  RtpOrigin& operator=(RtpOrigin&&) = delete;

 private:
  struct Origin {
    GstClockTime pts = GST_CLOCK_TIME_NONE;
    GstClockTime capture = GST_CLOCK_TIME_NONE;
  };

  using Pad = std::unique_ptr<GstPad, decltype(&gst_object_unref)>;
  static GstPadProbeReturn Remember(GstPad*, GstPadProbeInfo* info,
                                    gpointer data);
  static GstPadProbeReturn Restore(GstPad*, GstPadProbeInfo* info,
                                   gpointer data);
  std::mutex mutex_;
  static constexpr std::size_t kOriginCapacity = 64;
  std::array<Origin, kOriginCapacity> origins_{};
  std::size_t next_ = 0;
  Pad input_;
  Pad output_;
  gulong input_probe_ = 0;
  gulong output_probe_ = 0;
};
}  // namespace raven::media
