// SPDX-License-Identifier: AGPL-3.0-only
#include "rtp_origin.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>

#include <glib-object.h>
#include <glib.h>
#include <gst/rtp/gstrtphdrext.h>

namespace raven::media {
namespace {
using Element = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;
using Caps = std::unique_ptr<GstCaps, decltype(&gst_caps_unref)>;

GstElement* Named(GstElement* pipeline, const char* name) {
  // GstBin is the public checked cast for elements constructed by our graph.
  // NOLINTNEXTLINE(bugprone-casting-through-void)
  auto* element = gst_bin_get_by_name(GST_BIN(pipeline), name);
  if (!element) {
    throw std::runtime_error("RTP timing element is missing");
  }
  return element;
}

GstPad* NamedPad(GstElement* pipeline, const char* name,
                 GstPadDirection direction) {
  const Element element(Named(pipeline, name), gst_object_unref);
  auto* pad = gst_element_get_static_pad(
      element.get(), direction == GST_PAD_SINK ? "sink" : "src");
  if (!pad) {
    throw std::runtime_error("RTP timing pad is missing");
  }
  return pad;
}

void ConfigureExtension(GstElement* pipeline) {
  const Element payloader(Named(pipeline, "stream_rtp"), gst_object_unref);
  const Element extension(gst_element_factory_make("rtphdrextntp64", nullptr),
                          gst_object_unref);
  if (!extension) {
    throw std::runtime_error("RTP capture timestamp extension is unavailable");
  }
  // NOLINTNEXTLINE(bugprone-casting-through-void)
  gst_rtp_header_extension_set_id(GST_RTP_HEADER_EXTENSION(extension.get()), 1);
  g_signal_emit_by_name(payloader.get(), "add-extension", extension.get());
}
}  // namespace

RtpOrigin::RtpOrigin(GstElement* pipeline)
    : input_(NamedPad(pipeline, "video_encoder", GST_PAD_SINK),
             gst_object_unref),
      output_(NamedPad(pipeline, "video_parser", GST_PAD_SRC),
              gst_object_unref) {
  ConfigureExtension(pipeline);
  input_probe_ = gst_pad_add_probe(input_.get(), GST_PAD_PROBE_TYPE_BUFFER,
                                   Remember, this, nullptr);
  output_probe_ = gst_pad_add_probe(output_.get(), GST_PAD_PROBE_TYPE_BUFFER,
                                    Restore, this, nullptr);
}

RtpOrigin::~RtpOrigin() {
  gst_pad_remove_probe(input_.get(), input_probe_);
  gst_pad_remove_probe(output_.get(), output_probe_);
}

GstPadProbeReturn RtpOrigin::Remember(GstPad*, GstPadProbeInfo* info,
                                      gpointer data) {
  auto& self = *static_cast<RtpOrigin*>(data);
  auto* buffer = gst_pad_probe_info_get_buffer(info);
  const Caps reference(gst_caps_new_empty_simple("timestamp/x-ntp"),
                       gst_caps_unref);
  const auto* origin =
      gst_buffer_get_reference_timestamp_meta(buffer, reference.get());
  if (origin && GST_BUFFER_PTS_IS_VALID(buffer)) {
    const std::lock_guard<std::mutex> guard(self.mutex_);
    self.origins_.at(self.next_) = {GST_BUFFER_PTS(buffer), origin->timestamp};
    self.next_ = (self.next_ + 1) % self.origins_.size();
  }
  return GST_PAD_PROBE_OK;
}

GstPadProbeReturn RtpOrigin::Restore(GstPad*, GstPadProbeInfo* info,
                                     gpointer data) {
  auto& self = *static_cast<RtpOrigin*>(data);
  auto* buffer = gst_pad_probe_info_get_buffer(info);
  if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
    return GST_PAD_PROBE_OK;
  }
  GstClockTime capture = GST_CLOCK_TIME_NONE;
  {
    const std::lock_guard<std::mutex> guard(self.mutex_);
    const auto entry =
        std::find_if(self.origins_.begin(), self.origins_.end(),
                     [buffer](const Origin& value) {
                       return value.pts == GST_BUFFER_PTS(buffer);
                     });
    if (entry != self.origins_.end()) {
      capture = entry->capture;
      *entry = {};
    }
  }
  if (capture == GST_CLOCK_TIME_NONE) {
    return GST_PAD_PROBE_OK;
  }
  const Caps reference(gst_caps_new_empty_simple("timestamp/x-ntp"),
                       gst_caps_unref);
  // Copy the small encoded buffer header; its compressed payload stays shared.
  auto* output = gst_buffer_copy(buffer);
  if (!output) {
    g_warning("could not copy encoded header for capture timestamp");
    return GST_PAD_PROBE_DROP;
  }
  if (!gst_buffer_add_reference_timestamp_meta(output, reference.get(), capture,
                                               GST_CLOCK_TIME_NONE)) {
    gst_buffer_unref(output);
    g_warning("could not restore encoded capture timestamp");
    return GST_PAD_PROBE_DROP;
  }
  gst_pad_probe_info_set_buffer(info, output);
  return GST_PAD_PROBE_OK;
}
}  // namespace raven::media
