// SPDX-License-Identifier: AGPL-3.0-only
#include "vehicle_encoder.h"

#include <neutron_backend.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <ratio>
#include <stdexcept>
#include <string>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <tensorflow/lite/c/c_api.h>

#include "core/tracker.h"

namespace raven::tracking {
namespace {
constexpr int kWidth = 64;
constexpr int kHeight = 192;
constexpr int kChannels = 3;
constexpr int kMaximumDimension = 4096;
constexpr int kMinimumCrop = 16;
constexpr int kBitsPerChannel = 8;
constexpr int kDescriptorSize = 512;
constexpr double kHalf = 0.5;
constexpr float kPixelRange = 255.0F;
constexpr float kInt8Minimum = -128.0F;
constexpr float kInt8Maximum = 127.0F;
constexpr std::array<float, kChannels> kMean = {0.485F, 0.456F, 0.406F};
constexpr std::array<float, kChannels> kStddev = {0.229F, 0.224F, 0.225F};
using Pixbuf = std::unique_ptr<GdkPixbuf, decltype(&g_object_unref)>;

std::vector<std::int8_t> Input(const RgbFrame& frame, const Box& box,
                               TfLiteQuantizationParams quantization) {
  if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 ||
      frame.width > kMaximumDimension || frame.height > kMaximumDimension ||
      frame.stride < frame.width * kChannels ||
      static_cast<std::size_t>(frame.stride) * frame.height > frame.bytes ||
      !std::isfinite(box.center_x) || !std::isfinite(box.center_y) ||
      !std::isfinite(box.width) || !std::isfinite(box.height) ||
      box.width <= 0 || box.height <= 0) {
    throw std::invalid_argument("invalid RGB frame or vehicle crop");
  }
  const auto left = std::clamp(box.center_x - box.width * kHalf, 0.0,
                               static_cast<double>(frame.width));
  const auto top = std::clamp(box.center_y - box.height * kHalf, 0.0,
                              static_cast<double>(frame.height));
  const int x = static_cast<int>(std::floor(left));
  const int y = static_cast<int>(std::floor(top));
  const int right = static_cast<int>(
      std::ceil(std::clamp(box.center_x + box.width * kHalf, left,
                           static_cast<double>(frame.width))));
  const int bottom = static_cast<int>(
      std::ceil(std::clamp(box.center_y + box.height * kHalf, top,
                           static_cast<double>(frame.height))));
  if (right - x < kMinimumCrop || bottom - y < kMinimumCrop) {
    return {};
  }
  const Pixbuf source(
      gdk_pixbuf_new_from_data(frame.data, GDK_COLORSPACE_RGB, false,
                               kBitsPerChannel, frame.width, frame.height,
                               frame.stride, nullptr, nullptr),
      g_object_unref);
  if (!source) {
    throw std::runtime_error("cannot wrap RGB frame");
  }
  const Pixbuf crop(
      gdk_pixbuf_new_subpixbuf(source.get(), x, y, right - x, bottom - y),
      g_object_unref);
  const Pixbuf scaled(crop
                          ? gdk_pixbuf_scale_simple(crop.get(), kWidth, kHeight,
                                                    GDK_INTERP_BILINEAR)
                          : nullptr,
                      g_object_unref);
  if (!scaled) {
    throw std::runtime_error("cannot resize vehicle crop");
  }
  const auto* pixels = gdk_pixbuf_read_pixels(scaled.get());
  const int stride = gdk_pixbuf_get_rowstride(scaled.get());
  std::vector<std::int8_t> result(static_cast<std::size_t>(kWidth) * kHeight *
                                  kChannels);
  for (int row = 0; row < kHeight; ++row) {
    for (int column = 0; column < kWidth * kChannels; ++column) {
      const auto channel = column % kChannels;
      const auto value =
          (static_cast<float>(pixels[row * stride + column]) / kPixelRange -
           kMean[channel]) /
          kStddev[channel];
      const auto quantized = std::round(value / quantization.scale) +
                             static_cast<float>(quantization.zero_point);
      result[row * kWidth * kChannels + column] = static_cast<std::int8_t>(
          std::clamp(quantized, kInt8Minimum, kInt8Maximum));
    }
  }
  return result;
}

bool Shape(const TfLiteTensor* tensor, const std::vector<int>& shape) {
  if (tensor == nullptr || TfLiteTensorType(tensor) != kTfLiteInt8 ||
      TfLiteTensorNumDims(tensor) != static_cast<int>(shape.size()) ||
      !std::isfinite(TfLiteTensorQuantizationParams(tensor).scale) ||
      TfLiteTensorQuantizationParams(tensor).scale <= 0) {
    return false;
  }
  for (std::size_t axis = 0; axis < shape.size(); ++axis) {
    if (TfLiteTensorDim(tensor, static_cast<int>(axis)) != shape[axis]) {
      return false;
    }
  }
  return true;
}
}  // namespace

class VehicleEncoder::Impl {
 public:
  explicit Impl(const std::string& path)
      : model_(TfLiteModelCreateFromFile(path.c_str()), TfLiteModelDelete),
        options_(TfLiteInterpreterOptionsCreate(),
                 TfLiteInterpreterOptionsDelete),
        interpreter_(nullptr, TfLiteInterpreterDelete) {
    if (!model_ || !options_) {
      throw std::runtime_error("cannot load vehicle ReID model");
    }
    TfLiteInterpreterOptionsSetNumThreads(options_.get(), 1);
    TfLiteInterpreterOptionsAddDelegate(options_.get(), backend_.Get());
    interpreter_.reset(TfLiteInterpreterCreate(model_.get(), options_.get()));
    if (!interpreter_ ||
        TfLiteInterpreterAllocateTensors(interpreter_.get()) != kTfLiteOk ||
        backend_.Partitions() != 1 || backend_.CpuNodes() != 0) {
      throw std::runtime_error(
          "vehicle ReID requires one fully delegated NPU graph");
    }
    input_ = TfLiteInterpreterGetInputTensor(interpreter_.get(), 0);
    output_ = TfLiteInterpreterGetOutputTensor(interpreter_.get(), 0);
    if (TfLiteInterpreterGetInputTensorCount(interpreter_.get()) != 1 ||
        TfLiteInterpreterGetOutputTensorCount(interpreter_.get()) != 1 ||
        !Shape(input_, {1, kHeight, kWidth, kChannels}) ||
        !Shape(output_, {1, kDescriptorSize})) {
      throw std::runtime_error("incompatible vehicle ReID tensor contract");
    }
  }

  std::vector<float> Encode(const RgbFrame& frame, const Box& box);
  double milliseconds = 0;

 private:
  NeutronBackend backend_;
  std::unique_ptr<TfLiteModel, decltype(&TfLiteModelDelete)> model_;
  std::unique_ptr<TfLiteInterpreterOptions,
                  decltype(&TfLiteInterpreterOptionsDelete)>
      options_;
  std::unique_ptr<TfLiteInterpreter, decltype(&TfLiteInterpreterDelete)>
      interpreter_;
  TfLiteTensor* input_ = nullptr;
  const TfLiteTensor* output_ = nullptr;
};

std::vector<float> VehicleEncoder::Impl::Encode(const RgbFrame& frame,
                                                const Box& box) {
  const auto start = std::chrono::steady_clock::now();
  const auto input = Input(frame, box, TfLiteTensorQuantizationParams(input_));
  if (input.empty()) {
    return {};
  }
  if (TfLiteTensorCopyFromBuffer(input_, input.data(), input.size()) !=
          kTfLiteOk ||
      TfLiteInterpreterInvoke(interpreter_.get()) != kTfLiteOk) {
    throw std::runtime_error("vehicle ReID NPU invocation failed");
  }
  std::array<std::int8_t, kDescriptorSize> encoded{};
  if (TfLiteTensorCopyToBuffer(output_, encoded.data(), encoded.size()) !=
      kTfLiteOk) {
    throw std::runtime_error("cannot read vehicle ReID descriptor");
  }
  const auto quantization = TfLiteTensorQuantizationParams(output_);
  std::vector<float> result;
  result.reserve(encoded.size());
  for (const auto value : encoded) {
    result.push_back(
        static_cast<float>(static_cast<int>(value) - quantization.zero_point) *
        quantization.scale);
  }
  const auto norm = std::sqrt(
      std::inner_product(result.begin(), result.end(), result.begin(), 0.0F));
  if (!std::isfinite(norm) || norm <= 0) {
    throw std::runtime_error("invalid vehicle ReID descriptor norm");
  }
  for (auto& value : result) {
    value /= norm;
  }
  milliseconds = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start)
                     .count();
  return result;
}

VehicleEncoder::VehicleEncoder(const std::string& model)
    : impl_(std::make_unique<Impl>(model)) {}

VehicleEncoder::~VehicleEncoder() = default;

std::vector<float> VehicleEncoder::Encode(const RgbFrame& frame,
                                          const Box& box) {
  return impl_->Encode(frame, box);
}

double VehicleEncoder::LastMilliseconds() const { return impl_->milliseconds; }
}  // namespace raven::tracking
