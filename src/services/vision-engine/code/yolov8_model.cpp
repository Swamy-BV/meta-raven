// SPDX-License-Identifier: AGPL-3.0-only
#include "yolov8_model.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <tensorflow/lite/c/common.h>

namespace raven::vision {
namespace {
constexpr int kDimension = 640;
constexpr int kChannels = 3;
constexpr int kClasses = 80;
constexpr int kCandidates = 8400;
constexpr int kCoordinates = 4;
constexpr int kPadding = 114;
constexpr float kInputScale = 1.0f / static_cast<float>(UCHAR_MAX);
constexpr float kScaleTolerance = 1e-6f;
using Pixbuf = std::unique_ptr<GdkPixbuf, decltype(&g_object_unref)>;

void Require(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ValidateOutput(const TfLiteTensor* tensor, const int channels) {
  const auto quant = TfLiteTensorQuantizationParams(tensor);
  Require(TfLiteTensorType(tensor) == kTfLiteInt8 &&
              TfLiteTensorNumDims(tensor) == 3 &&
              TfLiteTensorDim(tensor, 0) == 1 &&
              TfLiteTensorDim(tensor, 1) == channels &&
              TfLiteTensorDim(tensor, 2) == kCandidates &&
              TfLiteTensorByteSize(tensor) ==
                  static_cast<size_t>(channels) * kCandidates &&
              std::isfinite(quant.scale) && quant.scale > 0 &&
              quant.zero_point >= SCHAR_MIN && quant.zero_point <= SCHAR_MAX,
          "YOLOv8 output contract mismatch");
}

std::vector<float> ReadOutput(const TfLiteTensor* tensor) {
  const auto size = TfLiteTensorByteSize(tensor);
  std::vector<int8_t> values(size);
  Require(TfLiteTensorCopyToBuffer(tensor, values.data(), size) == kTfLiteOk,
          "cannot read YOLOv8 output");
  const auto quant = TfLiteTensorQuantizationParams(tensor);
  std::vector<float> result(size);
  std::transform(
      values.begin(), values.end(), result.begin(),
      [quant](const int8_t value) {
        return static_cast<float>(static_cast<int>(value) - quant.zero_point) *
               quant.scale;
      });
  return result;
}

std::vector<uint8_t> PackLetterbox(GdkPixbuf* resized,
                                   const Letterbox& geometry) {
  constexpr auto kBytes =
      static_cast<size_t>(kDimension) * kDimension * kChannels;
  std::vector<uint8_t> bytes(kBytes, kPadding);
  const auto* pixels = gdk_pixbuf_read_pixels(resized);
  const auto stride = gdk_pixbuf_get_rowstride(resized);
  const auto channels = gdk_pixbuf_get_n_channels(resized);
  Require(channels == 3 || channels == 4, "unsupported image channels");
  for (int y = 0; y < geometry.height; ++y) {
    for (int x = 0; x < geometry.width; ++x) {
      const auto source =
          static_cast<size_t>(y) * stride + static_cast<size_t>(x) * channels;
      const auto destination =
          (static_cast<size_t>(y + geometry.top) * kDimension + x +
           geometry.left) *
          kChannels;
      std::copy_n(pixels + source, kChannels, bytes.data() + destination);
    }
  }
  return bytes;
}

int BestClass(const std::vector<float>& scores, const size_t candidate) {
  int best = 0;
  for (int cls = 1; cls < kClasses; ++cls) {
    if (scores[static_cast<size_t>(cls) * kCandidates + candidate] >
        scores[static_cast<size_t>(best) * kCandidates + candidate]) {
      best = cls;
    }
  }
  return best;
}
}  // namespace

void ValidateYoloV8(TfLiteInterpreter* interpreter) {
  Require(TfLiteInterpreterGetInputTensorCount(interpreter) == 1 &&
              TfLiteInterpreterGetOutputTensorCount(interpreter) == 2,
          "YOLOv8 requires one input and two outputs");
  const auto* input = TfLiteInterpreterGetInputTensor(interpreter, 0);
  const auto quant = TfLiteTensorQuantizationParams(input);
  Require(TfLiteTensorType(input) == kTfLiteUInt8 &&
              TfLiteTensorNumDims(input) == 4 &&
              TfLiteTensorDim(input, 0) == 1 &&
              TfLiteTensorDim(input, 1) == kDimension &&
              TfLiteTensorDim(input, 2) == kDimension &&
              TfLiteTensorDim(input, 3) == kChannels &&
              TfLiteTensorByteSize(input) ==
                  static_cast<size_t>(kDimension) * kDimension * kChannels &&
              quant.zero_point == 0 &&
              std::abs(quant.scale - kInputScale) < kScaleTolerance,
          "YOLOv8 input contract mismatch");
  ValidateOutput(TfLiteInterpreterGetOutputTensor(interpreter, 0),
                 kCoordinates);
  ValidateOutput(TfLiteInterpreterGetOutputTensor(interpreter, 1), kClasses);
}

Letterbox PrepareYoloV8(TfLiteInterpreter* interpreter,
                        const RgbLayout& image) {
  const auto width = image.width;
  const auto height = image.height;
  Require(image.source != nullptr &&
              (image.channels == 3 || image.channels == 4) &&
              image.stride >= width * image.channels,
          "invalid YOLOv8 RGB layout");
  const Pixbuf source(
      gdk_pixbuf_new_from_data(image.source, GDK_COLORSPACE_RGB,
                               image.channels == 4, CHAR_BIT, width, height,
                               image.stride, nullptr, nullptr),
      g_object_unref);
  Require(source != nullptr, "cannot wrap YOLOv8 RGB image");
  Require(width > 0 && height > 0, "invalid YOLOv8 source dimensions");
  const auto scale = std::min(static_cast<double>(kDimension) / width,
                              static_cast<double>(kDimension) / height);
  const auto resized_width =
      std::clamp(static_cast<int>(std::round(width * scale)), 1, kDimension);
  const auto resized_height =
      std::clamp(static_cast<int>(std::round(height * scale)), 1, kDimension);
  const Letterbox geometry{resized_width, resized_height,
                           (kDimension - resized_width) / 2,
                           (kDimension - resized_height) / 2};
  const Pixbuf resized(
      gdk_pixbuf_scale_simple(source.get(), resized_width, resized_height,
                              GDK_INTERP_BILINEAR),
      g_object_unref);
  Require(resized != nullptr, "YOLOv8 letterbox resize failed");
  const auto bytes = PackLetterbox(resized.get(), geometry);
  Require(TfLiteTensorCopyFromBuffer(
              TfLiteInterpreterGetInputTensor(interpreter, 0), bytes.data(),
              bytes.size()) == kTfLiteOk,
          "cannot fill YOLOv8 input");
  return geometry;
}

std::vector<detection::Box> DecodeYoloV8(const TfLiteInterpreter* interpreter,
                                         const Letterbox& geometry) {
  Require(geometry.width > 0 && geometry.height > 0,
          "missing YOLOv8 letterbox geometry");
  const auto boxes =
      ReadOutput(TfLiteInterpreterGetOutputTensor(interpreter, 0));
  const auto scores =
      ReadOutput(TfLiteInterpreterGetOutputTensor(interpreter, 1));
  std::vector<detection::Box> result;
  for (size_t index = 0; index < kCandidates; ++index) {
    const auto cls = BestClass(scores, index);
    const auto score = static_cast<double>(
        scores[static_cast<size_t>(cls) * kCandidates + index]);
    if (score < detection::kDefaultScoreThreshold) {
      continue;
    }
    const auto center_x = static_cast<double>(boxes[index]) * kDimension;
    const auto center_y =
        static_cast<double>(boxes[kCandidates + index]) * kDimension;
    const auto width =
        static_cast<double>(
            boxes[static_cast<size_t>(2) * kCandidates + index]) *
        kDimension;
    const auto height =
        static_cast<double>(
            boxes[static_cast<size_t>(3) * kCandidates + index]) *
        kDimension;
    result.push_back({(center_x - width / 2 - geometry.left) / geometry.width,
                      (center_y - height / 2 - geometry.top) / geometry.height,
                      (center_x + width / 2 - geometry.left) / geometry.width,
                      (center_y + height / 2 - geometry.top) / geometry.height,
                      score, cls});
  }
  return result;
}
}  // namespace raven::vision
