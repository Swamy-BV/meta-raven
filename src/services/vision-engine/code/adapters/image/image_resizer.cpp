// SPDX-License-Identifier: AGPL-3.0-only
#include "image_resizer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include "rgb_input.h"

namespace raven::vision {
namespace {
constexpr auto kMaxDimension = 4096;
constexpr auto kRgbChannels = 3;
constexpr auto kRgbaChannels = 4;
constexpr auto kBitsPerChannel = 8;
using Pixbuf = std::unique_ptr<GdkPixbuf, decltype(&g_object_unref)>;

void Validate(const RgbLayout& input, int width, int height) {
  if (!input.source || input.width < 1 || input.width > kMaxDimension ||
      input.height < 1 || input.height > kMaxDimension || width < 1 ||
      width > kMaxDimension || height < 1 || height > kMaxDimension ||
      (input.channels != kRgbChannels && input.channels != kRgbaChannels) ||
      input.stride < input.width * input.channels) {
    throw std::invalid_argument("invalid resize geometry or RGB layout");
  }
}

void RequireGpu() {
  cv::ocl::setUseOpenCL(true);
  if (!cv::ocl::useOpenCL() ||
      !(static_cast<unsigned>(cv::ocl::Device::getDefault().type()) &
        static_cast<unsigned>(cv::ocl::Device::TYPE_GPU))) {
    throw std::runtime_error("OpenCL preprocessing requires an accessible GPU");
  }
}
}  // namespace

bool IsResizeBackend(std::string_view backend) {
  return backend == "legacy" || backend == "cpu" || backend == "opencl";
}

class ImageResizer::Impl {
 public:
  explicit Impl(const std::string& backend) : backend_(backend) {
    if (!IsResizeBackend(backend)) {
      throw std::invalid_argument(
          "resize_backend must be legacy, cpu or opencl");
    }
    if (backend != "legacy") {
      cv::setNumThreads(1);
    }
    if (backend == "opencl") {
      RequireGpu();
    }
  }

  RgbLayout Resize(const RgbLayout& input, int width, int height) {
    Validate(input, width, height);
    if (input.width == width && input.height == height) {
      return input;
    }
    if (backend_ == "legacy") {
      return ResizeLegacy(input, width, height);
    }
    return ResizeOpenCv(input, width, height);
  }

  [[nodiscard]] const std::string& Backend() const { return backend_; }

 private:
  std::string backend_;
  Pixbuf legacy_{nullptr, g_object_unref};
  cv::Mat pixels_;
  cv::UMat uploaded_, resized_;

  RgbLayout ResizeLegacy(const RgbLayout& input, int width, int height) {
    const Pixbuf source(
        gdk_pixbuf_new_from_data(input.source, GDK_COLORSPACE_RGB,
                                 input.channels == kRgbaChannels,
                                 kBitsPerChannel, input.width, input.height,
                                 input.stride, nullptr, nullptr),
        g_object_unref);
    if (!source) {
      throw std::runtime_error("cannot wrap resize input");
    }
    legacy_.reset(gdk_pixbuf_scale_simple(source.get(), width, height,
                                          GDK_INTERP_BILINEAR));
    if (!legacy_) {
      throw std::runtime_error("legacy resize failed");
    }
    return {gdk_pixbuf_read_pixels(legacy_.get()), width, height,
            gdk_pixbuf_get_rowstride(legacy_.get()), input.channels};
  }

  RgbLayout ResizeOpenCv(const RgbLayout& input, int width, int height) {
    constexpr int kRgbType =
        cv::traits::Type<cv::Vec<unsigned char, kRgbChannels>>::value;
    constexpr int kRgbaType =
        cv::traits::Type<cv::Vec<unsigned char, kRgbaChannels>>::value;
    const auto type = input.channels == kRgbChannels ? kRgbType : kRgbaType;
    // OpenCV's external-buffer constructor requires mutable storage. This
    // const view is passed only as a read-only source to resize/copyTo.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    auto* pixels = const_cast<unsigned char*>(input.source);
    const cv::Mat source(input.height, input.width, type, pixels,
                         static_cast<std::size_t>(input.stride));
    if (backend_ == "opencl") {
      RequireGpu();
      source.copyTo(uploaded_);
      cv::resize(uploaded_, resized_, {width, height}, 0., 0.,
                 cv::INTER_LINEAR);
      resized_.copyTo(pixels_);
    } else {
      cv::resize(source, pixels_, {width, height}, 0., 0., cv::INTER_LINEAR);
    }
    return {pixels_.ptr<unsigned char>(), width, height,
            static_cast<int>(pixels_.step[0]), input.channels};
  }
};

ImageResizer::ImageResizer(const std::string& backend)
    : impl_(std::make_unique<Impl>(backend)) {}

ImageResizer::~ImageResizer() = default;

RgbLayout ImageResizer::Resize(const RgbLayout& input, int width, int height) {
  return impl_->Resize(input, width, height);
}

const std::string& ImageResizer::Backend() const { return impl_->Backend(); }
}  // namespace raven::vision
