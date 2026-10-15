// SPDX-License-Identifier: AGPL-3.0-only
#include "inference_worker.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <ratio>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <gsl/pointers>
#include <tensorflow/lite/c/c_api.h>
#include <tensorflow/lite/c/common.h>

#include "neutron_backend.h"
#include "rgb_input.h"
#include <detection.h>
#include <raven/contracts/camera.h>

namespace {
constexpr auto kExecFailureExitCode = 127;
constexpr auto kMaxImageDimension = 4096;
constexpr auto kWorkerChunkBytes = 65536;
constexpr auto kClassProbabilityOffset = 5;
constexpr auto kMaxClassifierDimension = 1024;
constexpr auto kChannelBits = 8;
constexpr auto kJpegMarkerPrefix = 0xff;
constexpr auto kJpegStartOfImage = 0xd8;
constexpr auto kMaxLabelBytes = 256;
constexpr auto kMaxLabels = 10000;
constexpr auto kYoloV4InputDimension = 416;
constexpr auto kCocoClassCount = 80;
constexpr auto kSignedByteOffset = 128;
constexpr auto kMaxByteFloat = 255.0f;
constexpr auto kQuantizationTolerance = 0.000001f;
constexpr auto kYoloV4OutputChannels = 255;
constexpr auto kYoloCoarseGrid = 13;
constexpr auto kYoloFineGrid = 26;
constexpr auto kSsdInputDimension = 300;
constexpr auto kSsdClassCount = 90;
constexpr auto kMaxSsdDetections = 100;
constexpr auto kClassifierNormalizationDivisor = 128.0f;
constexpr auto kMinFrameDimension = 64;
constexpr auto kMaxFrameWidth = 1920;
constexpr auto kMaxFrameHeight = 1080;
constexpr auto kYoloPredictionFields = 85;
constexpr auto kScoreThreshold = 0.5;
constexpr auto kScoreThresholdFloat = 0.5f;
constexpr auto kYoloCenterScale = 1.05;
constexpr auto kYoloCenterOffset = 0.025;
constexpr auto kMaxBoxExponent = 20.0;
constexpr auto kProbabilityUpperTolerance = 1.000001;
constexpr auto kWorkerReadChunkBytes = 4096;
constexpr auto kMaxWorkerRequestBytes = 4096;
constexpr auto kMaxEncodedImageBytesSigned = 16LL * 1024 * 1024;
constexpr auto kMaxEncodedImageBytes = 16ULL * 1024 * 1024;
constexpr auto kPngSignatureBytes = 8;
constexpr auto kMinEncodedImageBytes = 8;
constexpr auto kMinFrameBytes = 8;
constexpr auto kMaxQuantizedByte = 255;
constexpr auto kWorkerPollIntervalMs = 5;
}  // namespace

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

static void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

static double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

void ResidentWorker::Terminate() {
  healthy_ = false;
  if (pid_ > 0) {
    kill(-pid_, SIGKILL);
    kill(pid_, SIGKILL);
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
    pid_ = -1;
  }
  if (input_ >= 0) {
    close(input_);
  }
  if (output_ >= 0) {
    close(output_);
  }
  input_ = output_ = -1;
}

ResidentWorker::~ResidentWorker() { Terminate(); }

struct SpawnArguments {
  std::string model, labels, task, backend, threads;
};

static void ExecChild(const SpawnArguments& args, const int (&in)[2],
                      const int (&out)[2], pid_t parent) {
  prctl(PR_SET_PDEATHSIG, SIGKILL);
  if (getppid() != parent) {
    _exit(kExecFailureExitCode);
  }
  setpgid(0, 0);
  rlimit const memory{
      512ULL * kMaxClassifierDimension * kMaxClassifierDimension,
      512ULL * kMaxClassifierDimension * kMaxClassifierDimension};
  if (setrlimit(RLIMIT_AS, &memory)) {
    _exit(kExecFailureExitCode);
  }
  if (dup2(in[0], STDIN_FILENO) < 0 || dup2(out[1], STDOUT_FILENO) < 0) {
    _exit(kExecFailureExitCode);
  }
  close(in[0]);
  close(in[1]);
  close(out[0]);
  close(out[1]);
  // Fixed executable and argv; no shell or configurable plugin path.
  execl("/proc/self/exe", "model-runner", "--inference-worker",
        args.model.c_str(), args.labels.c_str(), args.task.c_str(),
        args.backend.c_str(), args.threads.c_str(),
        static_cast<char*>(nullptr));
  _exit(kExecFailureExitCode);
}

ResidentWorker::ResidentWorker(const WorkerSettings& settings,
                               std::chrono::steady_clock::time_point deadline,
                               std::atomic<bool>& cancel) {
  const auto& model = settings.model;
  const auto& labels = settings.labels;
  const auto& task = settings.task;
  const auto& backend = settings.backend;
  const auto threads = settings.threads;
  int in[2];
  int out[2];
  Require(pipe2(in, O_CLOEXEC) == 0, "cannot create worker input pipe");
  if (pipe2(out, O_CLOEXEC)) {
    close(in[0]);
    close(in[1]);
    throw std::runtime_error("cannot create worker output pipe");
  }
  const SpawnArguments args{model.string(), labels.string(), task, backend,
                            std::to_string(threads)};
  pid_t const parent = getpid();
  pid_ = fork();
  if (pid_ == 0) {
    ExecChild(args, in, out, parent);
  }
  close(in[0]);
  close(out[1]);
  input_ = in[1];
  output_ = out[0];
  if (pid_ < 0) {
    Terminate();
    throw std::runtime_error("cannot spawn interpreter");
  }
  setpgid(pid_, pid_);
  fcntl(input_, F_SETFL, O_NONBLOCK);
  fcntl(output_, F_SETFL, O_NONBLOCK);
  try {
    Exchange(nullptr, deadline, cancel);
    healthy_ = true;
  } catch (...) {
    Terminate();
    throw;
  }
}

void ResidentWorker::SendChunk(const std::string& message, size_t& sent) const {
  if (sent < message.size()) {
    ssize_t const n =
        write(input_, message.data() + sent, message.size() - sent);
    if (n > 0) {
      sent += static_cast<size_t>(n);
    } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
      throw std::runtime_error("worker input failed");
    }
  }
}

void ResidentWorker::ReadChunk() {
  char bytes[kWorkerReadChunkBytes];
  ssize_t const n = read(output_, bytes, sizeof(bytes));
  if (n > 0) {
    Require(buffered_.size() + static_cast<size_t>(n) <= kWorkerChunkBytes,
            "worker output limit exceeded");
    buffered_.append(bytes, static_cast<size_t>(n));
  } else if (n == 0) {
    throw std::runtime_error("interpreter exited unexpectedly");
  } else if (errno != EAGAIN && errno != EINTR) {
    throw std::runtime_error("worker output failed");
  }
}

Json ResidentWorker::DecodeResponse(size_t end) {
  Json response = Json::parse(buffered_.substr(0, end));
  buffered_.erase(0, end + 1);
  if (!response.at("ok").get<bool>()) {
    throw std::runtime_error(response.at("error").get<std::string>());
  }
  return response;
}

Json ResidentWorker::ExchangeLoop(const std::string& message,
                                  Clock::time_point deadline,
                                  std::atomic<bool>& cancel) {
  size_t sent = 0;
  for (;;) {
    Require(!cancel, "job cancelled");
    Require(Clock::now() < deadline, "worker timeout");
    SendChunk(message, sent);
    auto end = buffered_.find('\n');
    if (end != std::string::npos) {
      return DecodeResponse(end);
    }
    // Large raw frames need multiple pipe writes. Wait for either pipe,
    // rather than adding a response-only delay after every partial write.
    pollfd fds[]{{output_,
                  static_cast<short>(static_cast<unsigned>(POLLIN) |
                                     static_cast<unsigned>(POLLHUP)),
                  0},
                 {sent < message.size() ? input_ : -1, POLLOUT, 0}};
    if (poll(fds, 2, kWorkerPollIntervalMs) < 0 && errno != EINTR) {
      throw std::runtime_error("worker poll failed");
    }
    ReadChunk();
  }
}

Json ResidentWorker::Exchange(const Json* request,
                              std::chrono::steady_clock::time_point deadline,
                              std::atomic<bool>& cancel,
                              const std::vector<uint8_t>* payload) {
  std::string message = request ? request->dump() + "\n" : "";
  if (payload) {
    message.append(reinterpret_cast<const char*>(payload->data()),
                   payload->size());
  }
  try {
    return ExchangeLoop(message, deadline, cancel);
  } catch (...) {
    Terminate();
    throw;
  }
}

Json ResidentWorker::Infer(const std::filesystem::path& image,
                           std::chrono::steady_clock::time_point deadline,
                           std::atomic<bool>& cancel) {
  Json const request{{"image", image.string()}};
  return Exchange(&request, deadline, cancel);
}

Json ResidentWorker::InferFrame(const std::vector<uint8_t>& pixels, int width,
                                int height,
                                std::chrono::steady_clock::time_point deadline,
                                std::atomic<bool>& cancel) {
  Require(!pixels.empty() && pixels.size() <= raven::contracts::kMaxCameraBytes,
          "invalid frame payload");
  Json const request{{"format", "rgb"},
                     {"bytes", pixels.size()},
                     {"width", width},
                     {"height", height}};
  return Exchange(&request, deadline, cancel, &pixels);
}

namespace {
using Pixbuf = std::unique_ptr<GdkPixbuf, decltype(&g_object_unref)>;

struct Dimensions {
  bool invalid = false;
};

void Prepared(GdkPixbufLoader* loader, int width, int height, gpointer data) {
  auto& d = *static_cast<Dimensions*>(data);
  if (width < 1 || height < 1 || width > kMaxImageDimension ||
      height > kMaxImageDimension) {
    d.invalid = true;
    gdk_pixbuf_loader_set_size(loader, 1, 1);
  }
}

std::vector<uint8_t> ReadImage(const std::filesystem::path& path) {
  int fd =
      open(path.c_str(), static_cast<int>(static_cast<unsigned>(O_RDONLY) |
                                          static_cast<unsigned>(O_CLOEXEC) |
                                          static_cast<unsigned>(O_NOFOLLOW)));
  Require(fd >= 0, "cannot open input image");
  std::unique_ptr<int, void (*)(const int*)> const guard(
      &fd, [](const int* f) { close(*f); });
  struct stat st{};
  Require(fstat(fd, &st) == 0 &&
              ((st.st_mode & static_cast<mode_t>(S_IFMT)) ==
               static_cast<mode_t>(S_IFREG)) &&
              st.st_size > 0 && st.st_size <= kMaxEncodedImageBytesSigned,
          "invalid or oversized image");
  std::vector<uint8_t> bytes(static_cast<size_t>(st.st_size));
  size_t done = 0;
  while (done < bytes.size()) {
    ssize_t const n = read(fd, bytes.data() + done, bytes.size() - done);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    Require(n > 0, "image read failed");
    done += static_cast<size_t>(n);
  }
  return bytes;
}

Pixbuf Decode(const std::vector<uint8_t>& bytes) {
  Require(bytes.size() >= kMinEncodedImageBytes &&
              bytes.size() <= kMaxEncodedImageBytes,
          "invalid image size");
  const char* format = nullptr;
  if (bytes[0] == 'B' && bytes[1] == 'M') {
    format = "bmp";
  } else if (bytes[0] == kJpegMarkerPrefix && bytes[1] == kJpegStartOfImage) {
    format = "jpeg";
  } else if (std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n",
                         kPngSignatureBytes) == 0) {
    format = "png";
  }
  Require(format, "supported images are BMP, JPEG and PNG");
  GError* error = nullptr;
  GdkPixbufLoader* raw = gdk_pixbuf_loader_new_with_type(format, &error);
  if (error) {
    g_error_free(error);
  }
  Require(raw != nullptr, "image decoder unavailable");
  std::unique_ptr<GdkPixbufLoader, decltype(&g_object_unref)> const loader(
      raw, g_object_unref);
  Dimensions dimensions;
  g_signal_connect(raw, "size-prepared", G_CALLBACK(Prepared), &dimensions);
  bool valid = true;
  for (size_t pos = 0; pos < bytes.size(); pos += kWorkerChunkBytes) {
    if (!gdk_pixbuf_loader_write(
            raw, bytes.data() + pos,
            std::min<size_t>(kWorkerChunkBytes, bytes.size() - pos), nullptr) ||
        dimensions.invalid) {
      valid = false;
      break;
    }
  }
  valid = gdk_pixbuf_loader_close(raw, nullptr) && valid;
  Require(valid && !dimensions.invalid,
          "invalid image or dimensions exceed 4096 pixels");
  GdkPixbuf* pix = gdk_pixbuf_loader_get_pixbuf(raw);
  Require(pix && gdk_pixbuf_get_colorspace(pix) == GDK_COLORSPACE_RGB &&
              gdk_pixbuf_get_bits_per_sample(pix) == kChannelBits,
          "unsupported decoded image");
  // Keep the upstream checked GObject cast.
  // NOLINTNEXTLINE(bugprone-casting-through-void)
  return Pixbuf(GDK_PIXBUF(g_object_ref(pix)), g_object_unref);
}

struct ModelDelete {
  void operator()(TfLiteModel* p) const { TfLiteModelDelete(p); }
};

struct InterpreterDelete {
  void operator()(TfLiteInterpreter* p) const { TfLiteInterpreterDelete(p); }
};

struct OptionsDelete {
  void operator()(TfLiteInterpreterOptions* p) const {
    TfLiteInterpreterOptionsDelete(p);
  }
};

class Classifier : public InferenceModel {
  std::unique_ptr<NeutronBackend> neutron_;
  std::unique_ptr<TfLiteModel, ModelDelete> model_;
  std::unique_ptr<TfLiteInterpreter, InterpreterDelete> interpreter_;
  std::vector<std::string> labels_;
  int width_ = 0, height_ = 0, classes_ = 0;
  bool detector_ = false;
  bool yolo4_ = false;
  int max_detections_ = 0;
  double load_ms_;
  uint64_t count_ = 0;

  void ReadLabels(const char* label_file) {
    std::ifstream stream(label_file);
    Require(stream.good(), "cannot read labels");
    std::string label;
    while (std::getline(stream, label)) {
      if (!label.empty() && label.back() == '\r') {
        label.pop_back();
      }
      Require(!label.empty() && label.size() <= kMaxLabelBytes &&
                  labels_.size() < kMaxLabels &&
                  g_utf8_validate(label.data(),
                                  static_cast<gssize>(label.size()), nullptr),
              "invalid labels");
      labels_.push_back(label);
    }
    Require(stream.eof() && !labels_.empty(), "invalid labels");
  }

  // Arguments have distinct validated asset/option roles.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  void Initialize(const char* file, const char* backend, int threads) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    model_.reset(TfLiteModelCreateFromFile(file));
    Require(model_ != nullptr, "cannot load TFLite model");
    std::unique_ptr<TfLiteInterpreterOptions, OptionsDelete> const options(
        TfLiteInterpreterOptionsCreate());
    Require(options != nullptr, "cannot allocate interpreter options");
    TfLiteInterpreterOptionsSetNumThreads(options.get(), threads);
    Require(std::string(backend) == "cpu" || std::string(backend) == "neutron",
            "unsupported inference backend");
    if (std::string(backend) == "neutron") {
      neutron_ = std::make_unique<NeutronBackend>();
      TfLiteInterpreterOptionsAddDelegate(options.get(), neutron_->Get());
    }
    interpreter_.reset(TfLiteInterpreterCreate(model_.get(), options.get()));
    Require(interpreter_ && TfLiteInterpreterAllocateTensors(
                                interpreter_.get()) == kTfLiteOk,
            "cannot allocate interpreter tensors");
  }

  void ValidateYolo(const TfLiteTensor* input,
                    TfLiteQuantizationParams quant) const {
    Require(width_ == kYoloV4InputDimension &&
                height_ == kYoloV4InputDimension &&
                labels_.size() == kCocoClassCount &&
                quant.zero_point == -kSignedByteOffset &&
                std::abs(quant.scale - 1.0f / kMaxByteFloat) <
                    kQuantizationTolerance &&
                TfLiteTensorByteSize(input) ==
                    static_cast<size_t>(width_) * height_ * 3,
            "YOLOv4-tiny input contract mismatch");
    bool grids[2]{};
    for (int i = 0; i < 2; ++i) {
      const auto* out = TfLiteInterpreterGetOutputTensor(interpreter_.get(), i);
      Require(TfLiteTensorType(out) == kTfLiteFloat32 &&
                  TfLiteTensorNumDims(out) == 4 &&
                  TfLiteTensorDim(out, 0) == 1 &&
                  TfLiteTensorDim(out, 3) == kYoloV4OutputChannels,
              "YOLOv4-tiny output type/rank mismatch");
      int const grid = TfLiteTensorDim(out, 1);
      Require((grid == kYoloCoarseGrid || grid == kYoloFineGrid) &&
                  TfLiteTensorDim(out, 2) == grid &&
                  TfLiteTensorByteSize(out) ==
                      static_cast<size_t>(grid) * grid * kYoloV4OutputChannels *
                          sizeof(float),
              "YOLOv4-tiny output grid mismatch");
      int const index = grid == kYoloCoarseGrid ? 0 : 1;
      Require(!grids[index], "duplicate YOLOv4 output grid");
      grids[index] = true;
    }
  }

  void ValidateSsd(const TfLiteTensor* output) {
    Require(width_ == kSsdInputDimension && height_ == kSsdInputDimension &&
                labels_.size() == kSsdClassCount,
            "SSD adapter requires 300x300 input and 90 COCO label slots");
    Require(TfLiteTensorType(output) == kTfLiteFloat32 &&
                TfLiteTensorNumDims(output) == 3 &&
                TfLiteTensorDim(output, 0) == 1 &&
                TfLiteTensorDim(output, 2) == 4,
            "SSD boxes must have shape [1,N,4]");
    max_detections_ = TfLiteTensorDim(output, 1);
    Require(max_detections_ > 0 && max_detections_ <= kMaxSsdDetections &&
                TfLiteTensorByteSize(output) ==
                    static_cast<size_t>(max_detections_) * 4 * sizeof(float),
            "invalid SSD box count");
    for (int i = 1; i <= 2; ++i) {
      const auto* t = TfLiteInterpreterGetOutputTensor(interpreter_.get(), i);
      Require(TfLiteTensorType(t) == kTfLiteFloat32 &&
                  TfLiteTensorNumDims(t) == 2 && TfLiteTensorDim(t, 0) == 1 &&
                  TfLiteTensorDim(t, 1) == max_detections_ &&
                  TfLiteTensorByteSize(t) ==
                      static_cast<size_t>(max_detections_) * sizeof(float),
              "SSD classes/scores tensor mismatch");
    }
    const auto* count_tensor =
        TfLiteInterpreterGetOutputTensor(interpreter_.get(), 3);
    Require(TfLiteTensorType(count_tensor) == kTfLiteFloat32 &&
                TfLiteTensorNumDims(count_tensor) == 1 &&
                TfLiteTensorDim(count_tensor, 0) == 1 &&
                TfLiteTensorByteSize(count_tensor) == sizeof(float),
            "SSD count tensor mismatch");
  }

 public:
  // Arguments have distinct validated asset/option roles.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  Classifier(const char* file, const char* label_file, const char* task,
             const char* backend, int threads) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    Require(std::string(task) == "classification" ||
                std::string(task) == "detection" ||
                std::string(task) == "yolov4-tiny",
            "unsupported inference task");
    detector_ = std::string(task) == "detection";
    yolo4_ = std::string(task) == "yolov4-tiny";
    auto started = Clock::now();
    ReadLabels(label_file);
    // Arguments have distinct validated asset/option roles.
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    Initialize(file, backend, threads);
    Require(TfLiteInterpreterGetInputTensorCount(interpreter_.get()) == 1 &&
                TfLiteInterpreterGetOutputTensorCount(interpreter_.get()) ==
                    (yolo4_      ? 2
                     : detector_ ? 4
                                 : 1),
            "task output tensor count mismatch");
    auto* input = TfLiteInterpreterGetInputTensor(interpreter_.get(), 0);
    Require(TfLiteTensorType(input) == (yolo4_ ? kTfLiteInt8 : kTfLiteUInt8) &&
                TfLiteTensorNumDims(input) == 4 &&
                TfLiteTensorDim(input, 0) == 1 &&
                TfLiteTensorDim(input, 3) == 3,
            "adapter requires batch-one NHWC RGB uint8 input");
    height_ = TfLiteTensorDim(input, 1);
    width_ = TfLiteTensorDim(input, 2);
    auto quant = TfLiteTensorQuantizationParams(input);
    if (yolo4_) {
      // NOLINTEND(bugprone-easily-swappable-parameters)
      ValidateYolo(input, quant);
      load_ms_ = Milliseconds(started);
      return;
    }
    // Original MobileNetV1 models consume byte-valued RGB. Other quantization
    // or preprocessing contracts need their own explicit, tested adapter.
    Require(
        width_ > 0 && width_ <= kMaxClassifierDimension && height_ > 0 &&
            height_ <= kMaxClassifierDimension && std::isfinite(quant.scale) &&
            quant.scale > 0 &&
            ((quant.zero_point == kSignedByteOffset &&
              std::abs(quant.scale - 1.0f / kClassifierNormalizationDivisor) <
                  kQuantizationTolerance) ||
             (detector_ && quant.zero_point == 0 &&
              std::abs(quant.scale - 1.0f) < kQuantizationTolerance)) &&
            TfLiteTensorByteSize(input) ==
                static_cast<size_t>(width_) * height_ * 3,
        "unsupported input size or normalization contract");
    const auto* output =
        TfLiteInterpreterGetOutputTensor(interpreter_.get(), 0);
    if (detector_) {
      ValidateSsd(output);
      load_ms_ = Milliseconds(started);
      return;
    }
    Require(TfLiteTensorType(output) == kTfLiteUInt8 &&
                TfLiteTensorNumDims(output) == 2 &&
                TfLiteTensorDim(output, 0) == 1,
            "adapter requires uint8 class probabilities");
    classes_ = TfLiteTensorDim(output, 1);
    auto output_quant = TfLiteTensorQuantizationParams(output);
    Require(classes_ > 0 && static_cast<size_t>(classes_) == labels_.size() &&
                TfLiteTensorByteSize(output) == static_cast<size_t>(classes_) &&
                std::isfinite(output_quant.scale) && output_quant.scale > 0 &&
                output_quant.zero_point >= 0 &&
                output_quant.zero_point <= kMaxQuantizedByte,
            "output shape, labels or quantization mismatch");
    load_ms_ = Milliseconds(started);
  }

  Json Infer(const std::filesystem::path& image) override {
    auto started = Clock::now();
    return Classify(Decode(ReadImage(image)), started);
  }

  Json Infer(const std::vector<uint8_t>& bytes, int frame_width,
             int frame_height) override {
    auto started = Clock::now();
    Require(
        frame_width >= kMinFrameDimension && frame_width <= kMaxFrameWidth &&
            frame_height >= kMinFrameDimension &&
            frame_height <= kMaxFrameHeight &&
            bytes.size() == static_cast<size_t>(frame_width) * frame_height * 3,
        "invalid RGB frame dimensions");
    Pixbuf decoded(
        gdk_pixbuf_new_from_data(bytes.data(), GDK_COLORSPACE_RGB, false,
                                 kChannelBits, frame_width, frame_height,
                                 frame_width * 3, nullptr, nullptr),
        g_object_unref);
    Require(decoded != nullptr, "cannot wrap RGB frame");
    return Classify(std::move(decoded), started);
  }

  void PrepareTensor(const Pixbuf& decoded) const {
    Pixbuf const resized(gdk_pixbuf_scale_simple(decoded.get(), width_, height_,
                                                 GDK_INTERP_BILINEAR),
                         g_object_unref);
    Require(resized != nullptr, "image resize failed");
    const auto* pixels = gdk_pixbuf_read_pixels(resized.get());
    int const stride = gdk_pixbuf_get_rowstride(resized.get());
    int const channels = gdk_pixbuf_get_n_channels(resized.get());
    Require(channels == 3 || channels == 4, "unsupported image channels");
    auto* tensor = TfLiteInterpreterGetInputTensor(interpreter_.get(), 0);
    Require(TfLiteTensorByteSize(tensor) ==
                static_cast<size_t>(width_) * height_ * 3,
            "input tensor size changed");
    PackRgbTensor({pixels, width_, height_, stride, channels},
                  static_cast<uint8_t*>(TfLiteTensorData(tensor)), yolo4_);
  }

  struct YoloCell {
    int x = 0, y = 0, level = 0, stride = 0;
  };

  void AppendYoloCell(const std::vector<float>& values, int grid,
                      const YoloCell& cell,
                      std::vector<detection::Box>& boxes) const {
    const auto x = cell.x;
    const auto y = cell.y;
    const auto level = cell.level;
    const auto grid_stride = cell.stride;
    constexpr int kAnchors[2][3][2] = {{{81, 82}, {135, 169}, {344, 319}},
                                       {{23, 27}, {37, 58}, {81, 82}}};
    for (int a = 0; a < 3; ++a) {
      const float* p =
          values.data() +
          ((static_cast<size_t>(y) * grid + x) * 3 + a) * kYoloPredictionFields;
      double const objectness = detection::Sigmoid(p[4]);
      if (objectness < kScoreThreshold) {
        continue;
      }
      int const cls =
          static_cast<int>(std::max_element(p + kClassProbabilityOffset,
                                            p + kYoloPredictionFields) -
                           (p + kClassProbabilityOffset));
      double const score =
          objectness * detection::Sigmoid(p[kClassProbabilityOffset + cls]);
      if (score < kScoreThreshold) {
        continue;
      }
      double const cx =
          (detection::Sigmoid(static_cast<double>(p[0]) * kYoloCenterScale) -
           kYoloCenterOffset + x) *
          grid_stride;
      double const cy =
          (detection::Sigmoid(static_cast<double>(p[1]) * kYoloCenterScale) -
           kYoloCenterOffset + y) *
          grid_stride;
      double const bw =
          std::exp(std::clamp(static_cast<double>(p[2]), -kMaxBoxExponent,
                              kMaxBoxExponent)) *
          kAnchors[level][a][0];
      double const bh =
          std::exp(std::clamp(static_cast<double>(p[3]), -kMaxBoxExponent,
                              kMaxBoxExponent)) *
          kAnchors[level][a][1];
      boxes.push_back({(cx - bw / 2) / width_, (cy - bh / 2) / height_,
                       (cx + bw / 2) / width_, (cy + bh / 2) / height_, score,
                       cls});
    }
  }

  void AppendYoloOutput(int output_index,
                        std::vector<detection::Box>& boxes) const {
    const auto* out =
        TfLiteInterpreterGetOutputTensor(interpreter_.get(), output_index);
    int const grid = TfLiteTensorDim(out, 1);
    int const level = grid == kYoloCoarseGrid ? 0 : 1;
    int const grid_stride = width_ / grid;
    std::vector<float> values(TfLiteTensorByteSize(out) / sizeof(float));
    Require(TfLiteTensorCopyToBuffer(out, values.data(),
                                     TfLiteTensorByteSize(out)) == kTfLiteOk,
            "cannot read YOLOv4 output");
    for (int y = 0; y < grid; ++y) {
      for (int x = 0; x < grid; ++x) {
        AppendYoloCell(values, grid, {x, y, level, grid_stride}, boxes);
      }
    }
  }

  Json YoloResult(double preprocess, double invoke_ms) {
    std::vector<detection::Box> boxes;
    for (int output_index = 0; output_index < 2; ++output_index) {
      AppendYoloOutput(output_index, boxes);
    }
    return {{"ok", true},
            {"task", "detection"},
            {"predictions", Json::array()},
            {"detections", detection::Suppress(boxes, labels_)},
            {"score_threshold", kScoreThreshold},
            {"box_coordinates", "normalized-source-xyxy"},
            {"preprocess_ms", preprocess},
            {"inference_ms", invoke_ms},
            {"model_load_ms", load_ms_},
            {"worker_pid", getpid()},
            {"inference_count", ++count_},
            {"runtime", std::string("tensorflow-lite-") + TfLiteVersion()},
            {"backend", neutron_ ? "neutron" : "cpu"},
            {"npu_partitions", neutron_ ? neutron_->Partitions() : 0},
            {"cpu_nodes", neutron_ ? neutron_->CpuNodes() : 0}};
  }

  Json SsdResult(double preprocess, double invoke_ms) {
    std::array<std::vector<float>, 4> outputs;
    for (int i = 0; i < 4; ++i) {
      const auto* t = TfLiteInterpreterGetOutputTensor(interpreter_.get(), i);
      outputs[i].resize(TfLiteTensorByteSize(t) / sizeof(float));
      Require(TfLiteTensorCopyToBuffer(t, outputs[i].data(),
                                       TfLiteTensorByteSize(t)) == kTfLiteOk,
              "cannot read SSD output");
    }
    float const detection_count = outputs[3][0];
    Require(std::isfinite(detection_count) && detection_count >= 0 &&
                detection_count <= static_cast<float>(max_detections_) &&
                std::floor(detection_count) == detection_count,
            "invalid SSD detection count");
    Json detections = Json::array();
    for (int i = 0; i < static_cast<int>(detection_count); ++i) {
      float const cls = outputs[1][i];
      float score = outputs[2][i];
      Require(
          std::isfinite(cls) && cls >= 0 &&
              static_cast<double>(cls) < static_cast<double>(labels_.size()) &&
              std::floor(cls) == cls && std::isfinite(score) && score >= 0 &&
              score <= 1,
          "invalid SSD class or score");
      std::array<float, 4> box{};
      for (int c = 0; c < 4; ++c) {
        float const coordinate = outputs[0][4 * i + c];
        Require(std::isfinite(coordinate), "nonfinite SSD box");
        box[c] = std::clamp(coordinate, 0.0f, 1.0f);
      }
      Require(box[0] <= box[2] && box[1] <= box[3], "inverted SSD box");
      if (score < kScoreThresholdFloat || box[0] == box[2] ||
          box[1] == box[3]) {
        continue;
      }
      detections.push_back({{"class_id", static_cast<int>(cls)},
                            {"label", labels_[static_cast<size_t>(cls)]},
                            {"score", score},
                            {"box",
                             {{"xmin", box[1]},
                              {"ymin", box[0]},
                              {"xmax", box[3]},
                              {"ymax", box[2]}}}});
    }
    return {{"ok", true},
            {"task", "detection"},
            {"predictions", Json::array()},
            {"detections", detections},
            {"score_threshold", kScoreThreshold},
            {"box_coordinates", "normalized-source-xyxy"},
            {"preprocess_ms", preprocess},
            {"inference_ms", invoke_ms},
            {"model_load_ms", load_ms_},
            {"worker_pid", getpid()},
            {"inference_count", ++count_},
            {"runtime", std::string("tensorflow-lite-") + TfLiteVersion()},
            {"backend", neutron_ ? "neutron" : "cpu"},
            {"npu_partitions", neutron_ ? neutron_->Partitions() : 0},
            {"cpu_nodes", neutron_ ? neutron_->CpuNodes() : 0}};
  }

  Json Classify(Pixbuf decoded, Clock::time_point started) {
    PrepareTensor(decoded);
    double preprocess = Milliseconds(started);
    started = Clock::now();
    Require(TfLiteInterpreterInvoke(interpreter_.get()) == kTfLiteOk,
            "inference failed");
    double invoke_ms = Milliseconds(started);
    if (yolo4_) {
      return YoloResult(preprocess, invoke_ms);
    }
    if (detector_) {
      return SsdResult(preprocess, invoke_ms);
    }
    const auto* output =
        TfLiteInterpreterGetOutputTensor(interpreter_.get(), 0);
    std::vector<uint8_t> values(static_cast<size_t>(classes_));
    Require(TfLiteTensorCopyToBuffer(output, values.data(), values.size()) ==
                kTfLiteOk,
            "cannot read output tensor");
    auto quant = TfLiteTensorQuantizationParams(output);
    std::vector<size_t> indices(values.size());
    std::iota(indices.begin(), indices.end(), 0);
    size_t const top =
        std::min<size_t>(kClassProbabilityOffset, indices.size());
    std::partial_sort(
        indices.begin(), indices.begin() + static_cast<ptrdiff_t>(top),
        indices.end(), [&](size_t a, size_t b) {
          return values[a] != values[b] ? values[a] > values[b] : a < b;
        });
    Json results = Json::array();
    for (size_t i = 0; i < top; ++i) {
      size_t index = indices[i];
      double score = static_cast<float>(static_cast<int>(values[index]) -
                                        quant.zero_point) *
                     quant.scale;
      Require(std::isfinite(score) && score >= 0 &&
                  score <= kProbabilityUpperTolerance,
              "output is not a class probability");
      results.push_back(
          {{"label", labels_[index]}, {"score", score}, {"class_id", index}});
    }
    return {{"ok", true},
            {"predictions", results},
            {"preprocess_ms", preprocess},
            {"inference_ms", invoke_ms},
            {"model_load_ms", load_ms_},
            {"worker_pid", getpid()},
            {"inference_count", ++count_},
            {"runtime", std::string("tensorflow-lite-") + TfLiteVersion()},
            {"backend", neutron_ ? "neutron" : "cpu"},
            {"npu_partitions", neutron_ ? neutron_->Partitions() : 0},
            {"cpu_nodes", neutron_ ? neutron_->CpuNodes() : 0}};
  }
};
}  // namespace

std::vector<uint8_t> LoadRgbImage(const std::filesystem::path& image,
                                  int& width, int& height) {
  auto pix = Decode(ReadImage(image));
  width = gdk_pixbuf_get_width(pix.get());
  height = gdk_pixbuf_get_height(pix.get());
  int const channels = gdk_pixbuf_get_n_channels(pix.get());
  int const stride = gdk_pixbuf_get_rowstride(pix.get());
  const auto* source = gdk_pixbuf_read_pixels(pix.get());
  Require(channels == 3 || channels == 4, "unsupported image channels");
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  PackRgbTensor({source, width, height, stride, channels}, rgb.data(), false);
  return rgb;
}

static Json ProcessRequest(InferenceModel& classifier,
                           const std::string& line) {
  Json reply;
  Json request = Json::parse(line);
  if (request.is_object() && request.size() == 1 && request.contains("image")) {
    reply = classifier.Infer(request.at("image").get<std::string>());
  } else {
    Require(request.is_object() && request.size() == 4 &&
                request.value("format", "") == "rgb" &&
                request.contains("bytes") && request.contains("width") &&
                request.contains("height"),
            "invalid inference request");
    size_t const size = request.at("bytes");
    Require(size >= kMinFrameBytes && size <= raven::contracts::kMaxCameraBytes,
            "invalid frame size");
    std::vector<uint8_t> bytes(size);
    std::cin.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(size));
    Require(std::cin.good() && static_cast<size_t>(std::cin.gcount()) == size,
            "incomplete frame payload");
    reply = classifier.Infer(bytes, request.at("width"), request.at("height"));
  }
  return reply;
}

static int WorkerLoop(InferenceModel& classifier,
                      const std::function<bool(const Json&)>& send) {
  // Bounded framing, including malformed or partial input from a supervisor.
  for (;;) {
    std::string line;
    char c = 0;
    while (std::cin.get(c) && c != '\n') {
      Require(line.size() < kMaxWorkerRequestBytes, "oversized worker request");
      line.push_back(c);
    }
    if (!std::cin) {
      return line.empty() ? 0 : 1;
    }
    Json reply;
    try {
      reply = ProcessRequest(classifier, line);
    } catch (const std::exception& e) {
      reply = {{"ok", false}, {"error", e.what()}};
    }
    if (!send(reply)) {
      return 1;
    }
  }
}

int InferenceWorker(const char* model, const char* labels, const char* task,
                    const char* backend, int threads) {
  // Vendor delegates log to stdout. Keep the supervisor pipe JSON-only.
  int const protocol_fd = dup(STDOUT_FILENO);
  if (protocol_fd < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
    return 1;
  }
  auto close_protocol = [](gsl::owner<FILE*> file) { fclose(file); };
  std::unique_ptr<FILE, decltype(close_protocol)> protocol(
      fdopen(protocol_fd, "w"), close_protocol);
  if (!protocol) {
    close(protocol_fd);
    return 1;
  }
  auto send = [&](const Json& reply) {
    std::string const text = reply.dump() + '\n';
    return fputs(text.c_str(), protocol.get()) >= 0 &&
           fflush(protocol.get()) == 0;
  };
  try {
    Require(threads >= 1 && threads <= 4, "invalid thread count");
    std::unique_ptr<InferenceModel> classifier;
    if (std::string(task) == "yolox" || std::string(task) == "yolov6" ||
        std::string(task) == "yolov7") {
      Require(std::string(backend) == "cpu",
              "ONNX adapter requires CPU backend");
      classifier = MakeOnnxModel(model, labels, task, threads);
    } else {
      classifier =
          std::make_unique<Classifier>(model, labels, task, backend, threads);
    }
    if (!send(Json({{"ok", true}}))) {
      return 1;
    }
    return WorkerLoop(*classifier, send);
  } catch (const std::exception& e) {
    send(Json({{"ok", false}, {"error", e.what()}}));
    return 1;
  }
}
