// SPDX-License-Identifier: AGPL-3.0-only
#include "yolov8_model.h"

#include <algorithm>
#include <array>
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

// The interpreter owns these views until its next invocation. Decode completes
// synchronously before another invocation can overwrite the tensors.
struct QuantizedOutput {
  const int8_t* values;
  TfLiteQuantizationParams quantization;

  [[nodiscard]] double Value(const size_t index) const {
    return static_cast<float>(static_cast<int>(values[index]) -
                              quantization.zero_point) *
           quantization.scale;
  }
};

QuantizedOutput Output(const TfLiteTensor* tensor) {
  const auto* values = static_cast<const int8_t*>(TfLiteTensorData(tensor));
  Require(values != nullptr, "YOLOv8 output is not host accessible");
  return {values, TfLiteTensorQuantizationParams(tensor)};
}

void PackLetterbox(GdkPixbuf* resized, const Letterbox& geometry,
                   TfLiteTensor* tensor) {
  constexpr auto kBytes =
      static_cast<size_t>(kDimension) * kDimension * kChannels;
  Require(TfLiteTensorByteSize(tensor) == kBytes,
          "YOLOv8 input tensor size changed");
  auto* target = static_cast<uint8_t*>(TfLiteTensorData(tensor));
  Require(target != nullptr, "YOLOv8 input is not host accessible");
  std::fill_n(target, kBytes, kPadding);
  const RgbLayout layout{gdk_pixbuf_read_pixels(resized), geometry.width,
                         geometry.height, gdk_pixbuf_get_rowstride(resized),
                         gdk_pixbuf_get_n_channels(resized)};
  Require(layout.channels == 3 || layout.channels == 4,
          "unsupported image channels");
  for (int y = 0; y < geometry.height; ++y) {
    const auto destination =
        (static_cast<size_t>(y + geometry.top) * kDimension + geometry.left) *
        kChannels;
    PackRgbRow(layout.source + static_cast<size_t>(y) * layout.stride,
               target + destination, layout, 0);
  }
}

struct BestClasses {
  std::array<int8_t, kCandidates> scores{};
  std::array<uint8_t, kCandidates> ids{};
};

void UpdateClasses(const int8_t* plane, const uint8_t cls, BestClasses& best) {
  for (size_t index = 0; index < kCandidates; ++index) {
    if (plane[index] > best.scores[index]) {
      best.scores[index] = plane[index];
      best.ids[index] = cls;
    }
  }
}

BestClasses FindClasses(const QuantizedOutput& scores) {
  BestClasses best;
  std::copy_n(scores.values, kCandidates, best.scores.begin());
  // Scores share one positive quantization scale. Comparing quantized values
  // preserves argmax and first-class tie breaking, without dequantizing 672k
  // values. Scan each class contiguously rather than striding across planes.
  for (int cls = 1; cls < kClasses; ++cls) {
    UpdateClasses(scores.values + static_cast<size_t>(cls) * kCandidates,
                  static_cast<uint8_t>(cls), best);
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
  Pixbuf resized(nullptr, g_object_unref);
  if (resized_width != width || resized_height != height) {
    resized.reset(gdk_pixbuf_scale_simple(source.get(), resized_width,
                                          resized_height, GDK_INTERP_BILINEAR));
    Require(resized != nullptr, "YOLOv8 letterbox resize failed");
  }
  PackLetterbox(resized ? resized.get() : source.get(), geometry,
                TfLiteInterpreterGetInputTensor(interpreter, 0));
  return geometry;
}

std::vector<detection::Box> DecodeYoloV8(const TfLiteInterpreter* interpreter,
                                         const Letterbox& geometry) {
  Require(geometry.width > 0 && geometry.height > 0,
          "missing YOLOv8 letterbox geometry");
  const auto boxes = Output(TfLiteInterpreterGetOutputTensor(interpreter, 0));
  const auto scores = Output(TfLiteInterpreterGetOutputTensor(interpreter, 1));
  const auto best = FindClasses(scores);
  std::vector<detection::Box> result;
  for (size_t index = 0; index < kCandidates; ++index) {
    const auto cls = best.ids[index];
    const auto score =
        scores.Value(static_cast<size_t>(cls) * kCandidates + index);
    if (score < detection::kDefaultScoreThreshold) {
      continue;
    }
    const auto center_x = boxes.Value(index) * kDimension;
    const auto center_y = boxes.Value(kCandidates + index) * kDimension;
    const auto width =
        boxes.Value(static_cast<size_t>(2) * kCandidates + index) * kDimension;
    const auto height =
        boxes.Value(static_cast<size_t>(3) * kCandidates + index) * kDimension;
    result.push_back({(center_x - width / 2 - geometry.left) / geometry.width,
                      (center_y - height / 2 - geometry.top) / geometry.height,
                      (center_x + width / 2 - geometry.left) / geometry.width,
                      (center_y + height / 2 - geometry.top) / geometry.height,
                      score, cls});
  }
  return result;
}
}  // namespace raven::vision
