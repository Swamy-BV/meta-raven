// SPDX-License-Identifier: AGPL-3.0-only
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ratio>
#include <stdexcept>
#include <string>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <nlohmann/json.hpp>
#include <onnxruntime_c_api.h>

#include "detection.h"
#include "inference_worker.h"

namespace {
namespace {
constexpr auto kMaxLabelBytes = 256;
constexpr auto kCocoClassCount = 80;
constexpr auto kYoloxInputDimension = 416;
constexpr auto kYoloInputDimension = 640;
constexpr auto kYoloV7PredictionFields = 7;
constexpr auto kYoloGridPredictionFields = 85;
constexpr auto kPixelNormalizationDivisor = 255.0f;
constexpr auto kSmallGridStride = 8;
constexpr auto kMediumGridStride = 16;
constexpr auto kLargeGridStride = 32;
constexpr auto kMaxBoxExponent = 20.0;
constexpr auto kObjectnessThreshold = 0.5F;
constexpr auto kClassProbabilityOffset = 5;
constexpr auto kScoreThreshold = 0.5;
constexpr auto kMaxYoloV7Detections = 1000;
constexpr auto kYoloV7ScoreColumn = 6;
constexpr auto kYoloV7ClassColumn = 5;
constexpr auto kMaxImageDimension = 4096;
}  // namespace

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
const OrtApi* const kApi = OrtGetApiBase()->GetApi(ORT_API_VERSION);

void Check(OrtStatus* status) {
  if (status) {
    std::string const message = kApi->GetErrorMessage(status);
    kApi->ReleaseStatus(status);
    throw std::runtime_error(message);
  }
}

void Require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

double Elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}
template <class T>
using Ptr = std::unique_ptr<T, std::function<void(T*)>>;

struct Tensor {
  std::vector<int64_t> shape;
  ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
};

Tensor ReadTensor(OrtValue* value) {
  OrtTensorTypeAndShapeInfo* raw = nullptr;
  Check(kApi->GetTensorTypeAndShape(value, &raw));
  Ptr<OrtTensorTypeAndShapeInfo> const info(
      raw, kApi->ReleaseTensorTypeAndShapeInfo);
  size_t rank = 0;
  Tensor t;
  Check(kApi->GetDimensionsCount(info.get(), &rank));
  Require(rank > 0 && rank <= 4, "ONNX output rank unsupported");
  t.shape.resize(rank);
  Check(kApi->GetDimensions(info.get(), t.shape.data(), rank));
  Check(kApi->GetTensorElementType(info.get(), &t.type));
  return t;
}

Tensor Signature(OrtSession* session, bool input, size_t index) {
  OrtTypeInfo* raw = nullptr;
  Check(input ? kApi->SessionGetInputTypeInfo(session, index, &raw)
              : kApi->SessionGetOutputTypeInfo(session, index, &raw));
  Ptr<OrtTypeInfo> const info(raw, kApi->ReleaseTypeInfo);
  const OrtTensorTypeAndShapeInfo* t = nullptr;
  Check(kApi->CastTypeInfoToTensorInfo(info.get(), &t));
  Require(t != nullptr, "ONNX tensor required");
  size_t rank = 0;
  Tensor result;
  Check(kApi->GetDimensionsCount(t, &rank));
  Require(rank > 0 && rank <= 4, "ONNX tensor rank unsupported");
  result.shape.resize(rank);
  Check(kApi->GetDimensions(t, result.shape.data(), rank));
  Check(kApi->GetTensorElementType(t, &result.type));
  return result;
}

class OnnxModel : public InferenceModel {
  Ptr<OrtEnv> env_{nullptr, kApi->ReleaseEnv};
  Ptr<OrtSession> session_{nullptr, kApi->ReleaseSession};
  Ptr<OrtMemoryInfo> memory_{nullptr, kApi->ReleaseMemoryInfo};
  std::string adapter_, input_name_, output_name_;
  std::vector<std::string> labels_;
  int width_ = 0, height_ = 0;
  double load_ms_ = 0;
  uint64_t count_ = 0;

  void ReadLabels(const char* label_file) {
    std::ifstream stream(label_file);
    Require(stream.good(), "cannot read ONNX labels");
    std::string label;
    while (std::getline(stream, label)) {
      if (!label.empty() && label.back() == '\r') {
        label.pop_back();
      }
      Require(!label.empty() && label.size() <= kMaxLabelBytes &&
                  labels_.size() < kCocoClassCount &&
                  g_utf8_validate(label.data(),
                                  static_cast<gssize>(label.size()), nullptr),
              "invalid ONNX labels");
      labels_.push_back(label);
    }
    Require(stream.eof() && labels_.size() == kCocoClassCount,
            "80 COCO labels required");
  }

  void Initialize(const char* file, int threads) {
    OrtEnv* raw_env = nullptr;
    Check(
        kApi->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "vision-engine", &raw_env));
    env_.reset(raw_env);
    Check(kApi->DisableTelemetryEvents(env_.get()));
    OrtSessionOptions* raw_options = nullptr;
    Check(kApi->CreateSessionOptions(&raw_options));
    Ptr<OrtSessionOptions> const options(raw_options,
                                         kApi->ReleaseSessionOptions);
    Check(kApi->SetIntraOpNumThreads(options.get(), threads));
    Check(kApi->SetInterOpNumThreads(options.get(), 1));
    Check(kApi->SetSessionExecutionMode(options.get(), ORT_SEQUENTIAL));
    Check(
        kApi->SetSessionGraphOptimizationLevel(options.get(), ORT_ENABLE_ALL));
    OrtSession* raw_session = nullptr;
    Check(kApi->CreateSession(env_.get(), file, options.get(), &raw_session));
    session_.reset(raw_session);
  }

 public:
  // Arguments have distinct validated asset/option roles.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  OnnxModel(const char* file, const char* label_file, const char* kind,
            int threads)
      : adapter_(kind) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    Require(kApi != nullptr, "ONNX runtime API unavailable");
    auto start = Clock::now();
    ReadLabels(label_file);
    Initialize(file, threads);
    size_t inputs = 0;
    size_t outputs = 0;
    Check(kApi->SessionGetInputCount(session_.get(), &inputs));
    Check(kApi->SessionGetOutputCount(session_.get(), &outputs));
    Require(inputs == 1 && outputs == 1,
            "ONNX adapter requires one input/output");
    auto input = Signature(session_.get(), true, 0);
    if (input.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        input.shape.size() != 4 ||
        (input.shape[0] != 1 && input.shape[0] != -1) || input.shape[1] != 3) {
      throw std::runtime_error(
          "ONNX input must be batch-one float32 NCHW: " +
          Json({{"shape", input.shape}, {"type", static_cast<int>(input.type)}})
              .dump());
    }
    height_ = static_cast<int>(input.shape[2]);
    width_ = static_cast<int>(input.shape[3]);
    Require(width_ == height_ &&
                ((adapter_ == "yolox" && width_ == kYoloxInputDimension) ||
                 ((adapter_ == "yolov6" || adapter_ == "yolov7") &&
                  width_ == kYoloInputDimension)),
            "ONNX model input size/adapter mismatch");
    auto output = Signature(session_.get(), false, 0);
    Require(output.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
                ((adapter_ == "yolov7" && output.shape.size() == 2 &&
                  output.shape[1] == kYoloV7PredictionFields) ||
                 (adapter_ != "yolov7" && output.shape.size() == 3 &&
                  output.shape[2] == kYoloGridPredictionFields)),
            "ONNX detector output contract mismatch");
    OrtAllocator* allocator = nullptr;
    Check(kApi->GetAllocatorWithDefaultOptions(&allocator));
    char* name = nullptr;
    Check(kApi->SessionGetInputName(session_.get(), 0, allocator, &name));
    input_name_ = name;
    allocator->Free(allocator, name);
    Check(kApi->SessionGetOutputName(session_.get(), 0, allocator, &name));
    output_name_ = name;
    allocator->Free(allocator, name);
    OrtMemoryInfo* raw_memory = nullptr;
    Check(kApi->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                    &raw_memory));
    memory_.reset(raw_memory);
    load_ms_ = Elapsed(start);
  }

  Json Infer(const std::filesystem::path& file) override {
    int w = 0;
    int h = 0;
    auto rgb = LoadRgbImage(file, w, h);
    return Infer(rgb, w, h);
  }

  struct Geometry {
    int w = 0, h = 0, rw = 0, rh = 0, left = 0, top = 0;
    double ratio = 0.0;
  };

  struct RawBox {
    double cx = 0.0, cy = 0.0, bw = 0.0, bh = 0.0, score = 0.0;
    int cls = 0;
  };

  [[nodiscard]] Geometry ResizeGeometry(int w, int h) const {
    Geometry geometry;
    geometry.w = w;
    geometry.h = h;
    geometry.ratio = std::min(static_cast<double>(width_) / w,
                              static_cast<double>(height_) / h);
    geometry.rw = adapter_ == "yolox"
                      ? static_cast<int>(w * geometry.ratio)
                      : static_cast<int>(std::round(w * geometry.ratio));
    geometry.rh = adapter_ == "yolox"
                      ? static_cast<int>(h * geometry.ratio)
                      : static_cast<int>(std::round(h * geometry.ratio));
    geometry.left = adapter_ == "yolox" ? 0 : (width_ - geometry.rw) / 2;
    geometry.top = adapter_ == "yolox" ? 0 : (height_ - geometry.rh) / 2;
    Require(geometry.rw > 0 && geometry.rh > 0, "invalid ONNX resize");
    return geometry;
  }

  void PackRow(const GdkPixbuf* image, int y, const Geometry& geometry,
               std::vector<float>& pixels) const {
    const auto* source = gdk_pixbuf_read_pixels(image);
    const int stride = gdk_pixbuf_get_rowstride(image);
    const bool raw_bgr = adapter_ == "yolox";
    for (int x = 0; x < geometry.rw; ++x) {
      for (int c = 0; c < 3; ++c) {
        const int channel = raw_bgr ? 2 - c : c;
        pixels[(static_cast<size_t>(c) * height_ + y + geometry.top) * width_ +
               x + geometry.left] =
            static_cast<float>(source[static_cast<size_t>(y) * stride +
                                      static_cast<size_t>(x) * 3 + channel]) /
            (raw_bgr ? 1.0f : kPixelNormalizationDivisor);
      }
    }
  }

  [[nodiscard]] std::vector<float> PrepareInput(
      const std::vector<uint8_t>& rgb, const Geometry& geometry) const {
    Ptr<GdkPixbuf> const image(
        gdk_pixbuf_new_from_data(rgb.data(), GDK_COLORSPACE_RGB, false,
                                 kSmallGridStride, geometry.w, geometry.h,
                                 geometry.w * 3, nullptr, nullptr),
        g_object_unref);
    Require(image != nullptr, "cannot wrap ONNX image");
    Ptr<GdkPixbuf> const resized(
        gdk_pixbuf_scale_simple(image.get(), geometry.rw, geometry.rh,
                                GDK_INTERP_BILINEAR),
        g_object_unref);
    Require(resized != nullptr, "ONNX image resize failed");
    const float pad =
        adapter_ == "yolox" ? 114.0f : 114.0f / kPixelNormalizationDivisor;
    std::vector<float> pixels(static_cast<size_t>(width_) * height_ * 3, pad);
    for (int y = 0; y < geometry.rh; ++y) {
      PackRow(resized.get(), y, geometry, pixels);
    }
    return pixels;
  }

  static detection::Box ConvertBox(const RawBox& box,
                                   const Geometry& geometry) {
    Require(box.bw >= 0 && box.bh >= 0, "negative YOLO box dimensions");
    return {(box.cx - box.bw / 2 - geometry.left) / geometry.ratio / geometry.w,
            (box.cy - box.bh / 2 - geometry.top) / geometry.ratio / geometry.h,
            (box.cx + box.bw / 2 - geometry.left) / geometry.ratio / geometry.w,
            (box.cy + box.bh / 2 - geometry.top) / geometry.ratio / geometry.h,
            box.score,
            box.cls};
  }

  void TransformYolox(RawBox& box, int index) const {
    int base = 0;
    int grid_stride = kSmallGridStride;
    for (const int candidate :
         {kSmallGridStride, kMediumGridStride, kLargeGridStride}) {
      const int cells = (width_ / candidate) * (height_ / candidate);
      if (index < base + cells) {
        grid_stride = candidate;
        break;
      }
      base += cells;
    }
    const int grid = width_ / grid_stride;
    const int local = index - base;
    box.cx = (box.cx + local % grid) * grid_stride;
    const int cell_row = local / grid;  // Grid row requires integer division.
    box.cy = (box.cy + static_cast<double>(cell_row)) * grid_stride;
    box.bw = std::exp(std::clamp(box.bw, -kMaxBoxExponent, kMaxBoxExponent)) *
             grid_stride;
    box.bh = std::exp(std::clamp(box.bh, -kMaxBoxExponent, kMaxBoxExponent)) *
             grid_stride;
  }

  std::optional<RawBox> GridBox(const float* row, int index) const {
    Require(std::isfinite(row[4]) && row[4] >= 0 && row[4] <= 1,
            "invalid YOLO objectness");
    if (row[4] < kObjectnessThreshold) {
      return std::nullopt;
    }
    const int cls =
        static_cast<int>(std::max_element(row + kClassProbabilityOffset,
                                          row + kYoloGridPredictionFields) -
                         (row + kClassProbabilityOffset));
    Require(std::isfinite(row[kClassProbabilityOffset + cls]) &&
                row[kClassProbabilityOffset + cls] >= 0 &&
                row[kClassProbabilityOffset + cls] <= 1,
            "invalid YOLO class score");
    const double score = row[4] * row[kClassProbabilityOffset + cls];
    if (score < kScoreThreshold) {
      return std::nullopt;
    }
    RawBox box{row[0], row[1], row[2], row[3], score, cls};
    if (adapter_ == "yolox") {
      TransformYolox(box, index);
    }
    return box;
  }

  static std::vector<detection::Box> DecodeV7(const Tensor& output,
                                              const float* pixels,
                                              const Geometry& geometry) {
    Require(output.shape.size() == 2 &&
                output.shape[1] == kYoloV7PredictionFields &&
                output.shape[0] >= 0 && output.shape[0] <= kMaxYoloV7Detections,
            "YOLOv7 result shape mismatch");
    std::vector<detection::Box> boxes;
    for (int64_t i = 0; i < output.shape[0]; ++i) {
      const float* row = pixels + i * kYoloV7PredictionFields;
      Require(
          std::isfinite(row[0]) && row[0] == 0 &&
              std::isfinite(row[kYoloV7ClassColumn]) &&
              row[kYoloV7ClassColumn] >= 0 &&
              row[kYoloV7ClassColumn] < kCocoClassCount &&
              std::floor(row[kYoloV7ClassColumn]) == row[kYoloV7ClassColumn],
          "YOLOv7 batch/class invalid");
      boxes.push_back(
          ConvertBox({(row[1] + row[3]) / 2, (row[2] + row[4]) / 2,
                      row[3] - row[1], row[4] - row[2], row[kYoloV7ScoreColumn],
                      static_cast<int>(row[kYoloV7ClassColumn])},
                     geometry));
    }
    return boxes;
  }

  std::vector<detection::Box> DecodeGrid(const Tensor& output,
                                         const float* pixels,
                                         const Geometry& geometry) const {
    const int expected = adapter_ == "yolox" ? 3549 : 8400;
    Require(output.shape ==
                std::vector<int64_t>({1, expected, kYoloGridPredictionFields}),
            "YOLO grid output shape mismatch");
    std::vector<detection::Box> boxes;
    for (int i = 0; i < expected; ++i) {
      const auto box = GridBox(
          pixels + static_cast<size_t>(i) * kYoloGridPredictionFields, i);
      if (box) {
        boxes.push_back(ConvertBox(*box, geometry));
      }
    }
    return boxes;
  }

  Json Infer(const std::vector<uint8_t>& rgb, int w, int h) override {
    auto start = Clock::now();
    Require(w > 0 && w <= kMaxImageDimension && h > 0 &&
                h <= kMaxImageDimension &&
                rgb.size() == static_cast<size_t>(w) * h * 3,
            "invalid ONNX RGB input");
    const auto geometry = ResizeGeometry(w, h);
    auto pixels = PrepareInput(rgb, geometry);
    std::array<int64_t, 4> shape{1, 3, height_, width_};
    OrtValue* raw_input = nullptr;
    Check(kApi->CreateTensorWithDataAsOrtValue(
        memory_.get(), pixels.data(), pixels.size() * sizeof(float),
        shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &raw_input));
    Ptr<OrtValue> const input(raw_input, kApi->ReleaseValue);
    const double preprocess = Elapsed(start);
    start = Clock::now();
    const char* in_name = input_name_.c_str();
    const char* out_name = output_name_.c_str();
    const OrtValue* in_value = input.get();
    OrtValue* raw_output = nullptr;
    Check(kApi->Run(session_.get(), nullptr, &in_name, &in_value, 1, &out_name,
                    1, &raw_output));
    Ptr<OrtValue> const output(raw_output, kApi->ReleaseValue);
    const double invoke = Elapsed(start);
    const auto out = ReadTensor(output.get());
    Require(out.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
            "ONNX output must be float32");
    float* data = nullptr;
    Check(kApi->GetTensorMutableData(output.get(),
                                     reinterpret_cast<void**>(&data)));
    auto boxes = adapter_ == "yolov7" ? DecodeV7(out, data, geometry)
                                      : DecodeGrid(out, data, geometry);
    return {{"ok", true},
            {"task", "detection"},
            {"predictions", Json::array()},
            {"detections", detection::Suppress(boxes, labels_)},
            {"score_threshold", kScoreThreshold},
            {"box_coordinates", "normalized-source-xyxy"},
            {"preprocess_ms", preprocess},
            {"inference_ms", invoke},
            {"model_load_ms", load_ms_},
            {"worker_pid", getpid()},
            {"inference_count", ++count_},
            {"runtime",
             std::string("onnxruntime-") + OrtGetApiBase()->GetVersionString()},
            {"backend", "cpu"}};
  }
};
}  // namespace

std::unique_ptr<InferenceModel> MakeOnnxModel(const char* model,
                                              const char* labels,
                                              const char* adapter,
                                              int threads) {
  return std::make_unique<OnnxModel>(model, labels, adapter, threads);
}
