// SPDX-License-Identifier: AGPL-3.0-only
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <glib-unix.h>
#include <glib.h>
#include <nlohmann/json.hpp>
#include <rclcpp/init_options.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/ros/frame_input.h"
#include "inference_worker.h"
#include <local_ipc.h>
#include <raven/contracts/camera.h>

namespace {
constexpr auto kMaxConfigBytes = 65536;
constexpr auto kTfliteHeaderBytes = 8;
constexpr auto kDefaultFrameAgeMs = 500;
constexpr auto kMinFrameAgeMs = 20;
constexpr auto kMaxFrameAgeMs = 2000;
constexpr auto kMaxJobTimeoutSeconds = 120;
constexpr auto kMaxModelCount = 64;
constexpr auto kMaxLicenseTextBytes = 128;
constexpr auto kMaxModelMiB = 256ULL;
constexpr auto kKiB = 1024ULL;
constexpr auto kKiBInt = 1024;
constexpr auto kMicrosPerMillisecond = 1000;
constexpr auto kMicrosPerMillisecondDouble = 1000.0;
constexpr auto kMaxRequestBytes = 4096;
constexpr auto kClientPollIntervalMs = 50;
constexpr auto kRequestReadChunkBytes = 512;
constexpr auto kSocketPermissions = 0600;
constexpr auto kMaxResponseBytes = 131072;
constexpr auto kWorkerArgumentCount = 7;
constexpr auto kWorkerBackendArgumentIndex = 5;
constexpr auto kWorkerThreadsArgumentIndex = 6;
constexpr auto kPrivateFileUmask = 0077;
constexpr auto kDigestChunkBytes = 65536;
constexpr auto kListenBacklog = 8;
}  // namespace

using Json = nlohmann::json;
namespace fs = std::filesystem;

static void Fail(const std::string& message) {
  throw std::runtime_error(message);
}

static bool Token(const std::string& s) {
  return std::regex_match(s, std::regex("[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}"));
}

static void Keys(const Json& j, const std::vector<std::string>& allowed) {
  if (!j.is_object()) {
    Fail("expected a JSON object");
  }
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end()) {
      Fail("unknown field: " + it.key());
    }
  }
}

static Json ReadJson(const fs::path& path) {
  if (!fs::is_regular_file(path) || fs::file_size(path) > kMaxConfigBytes) {
    Fail("invalid or oversized JSON file");
  }
  std::ifstream stream(path);
  if (!stream) {
    Fail("cannot read JSON file");
  }
  return Json::parse(stream);
}

static bool InvalidModelPrefix(bool model, size_t total, ssize_t count,
                               const char* bytes) {
  return model && total == 0 &&
         (count < kTfliteHeaderBytes || std::memcmp(bytes + 4, "TFL3", 4) != 0);
}

static std::string Digest(const fs::path& file, size_t limit,
                          bool model = false) {
  int const fd =
      open(file.c_str(), static_cast<int>(static_cast<unsigned>(O_RDONLY) |
                                          static_cast<unsigned>(O_CLOEXEC) |
                                          static_cast<unsigned>(O_NOFOLLOW)));
  if (fd < 0) {
    Fail("cannot open asset");
  }
  struct stat st{};
  if (fstat(fd, &st) ||
      !((st.st_mode & static_cast<mode_t>(S_IFMT)) ==
        static_cast<mode_t>(S_IFREG)) ||
      st.st_size <= 0 || static_cast<size_t>(st.st_size) > limit) {
    close(fd);
    Fail("invalid or oversized asset");
  }
  GChecksum* sum = g_checksum_new(G_CHECKSUM_SHA256);
  char bytes[kDigestChunkBytes];
  size_t total = 0;
  for (;;) {
    // Selection deliberately serializes bounded regular-file checksum reads
    // with model-state changes; unlocking here would permit stale validation.
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    ssize_t const n = read(fd, bytes, sizeof(bytes));
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      close(fd);
      g_checksum_free(sum);
      Fail("asset read failed");
    }
    if (!n) {
      break;
    }
    if (InvalidModelPrefix(model, total, n, bytes)) {
      close(fd);
      g_checksum_free(sum);
      Fail("not a TensorFlow Lite model");
    }
    total += static_cast<size_t>(n);
    if (total > limit) {
      close(fd);
      g_checksum_free(sum);
      Fail("asset grew beyond size limit");
    }
    g_checksum_update(sum, reinterpret_cast<const guchar*>(bytes), n);
  }
  close(fd);
  std::string result = g_checksum_get_string(sum);
  g_checksum_free(sum);
  return result;
}

struct Settings {
  fs::path registry, assets, demo, state, inputs;
  std::string socket, camera_topic;
  int frame_max_age;
  int threads, timeout;

  explicit Settings(const Json& j) {
    Keys(j,
         {"schema", "registry", "assets", "demo", "state", "inputs", "socket",
          "threads", "timeout_seconds", "camera_topic", "frame_max_age_ms"});
    if (j.at("schema") != 3) {
      Fail("unsupported configuration schema");
    }
    registry = j.at("registry").get<std::string>();
    assets = j.at("assets").get<std::string>();
    demo = j.at("demo").get<std::string>();
    state = j.at("state").get<std::string>();
    inputs = j.at("inputs").get<std::string>();
    socket = j.at("socket");
    camera_topic = j.at("camera_topic").get<std::string>();
    frame_max_age = j.value("frame_max_age_ms", kDefaultFrameAgeMs);
    if (camera_topic.empty() || frame_max_age < kMinFrameAgeMs ||
        frame_max_age > kMaxFrameAgeMs) {
      Fail("invalid camera topic or frame age");
    }
    threads = j.at("threads");
    timeout = j.at("timeout_seconds");
    if (threads < 1 || threads > 4 || timeout < 1 ||
        timeout > kMaxJobTimeoutSeconds || socket.empty() ||
        socket.size() >= sizeof(sockaddr_un::sun_path)) {
      Fail("configuration outside supported bounds");
    }
    for (const auto& path : {registry, assets, demo, state, inputs}) {
      if (path.empty()) {
        Fail("empty configured path");
      }
    }
    if (!fs::is_regular_file(demo)) {
      Fail("validation image unavailable");
    }
  }
};

struct Model {
  std::string id, version, file, sha, labels, labels_sha, task, backend,
      license, adapter;

  [[nodiscard]] Json Describe() const {
    return {{"id", id},
            {"version", version},
            {"sha256", sha},
            {"task", task},
            {"backend", backend},
            {"license", license},
            {"adapter", adapter},
            {"labels", labels},
            {"labels_sha256", labels_sha}};
  }
};

static std::vector<fs::path> CatalogFiles(const fs::path& extra_dir) {
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(extra_dir)) {
    if (entry.path().extension() != ".json") {
      continue;
    }
    if (!fs::is_regular_file(entry.symlink_status())) {
      Fail("invalid optional catalog file");
    }
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  if (files.size() > kMaxModelCount) {
    Fail("too many optional model catalogs");
  }
  return files;
}

static void MergeOptionalCatalogs(Json& j, const Settings& c) {
  auto extra_dir = c.registry.parent_path() / "models.d";
  if (!fs::is_directory(extra_dir)) {
    return;
  }

  const auto files = CatalogFiles(extra_dir);
  for (const auto& file : files) {
    auto extra = ReadJson(file);
    Keys(extra, {"schema", "models"});
    if (extra.at("schema") != 1 || !extra.at("models").is_array() ||
        extra.at("models").size() > kMaxModelCount) {
      Fail("invalid optional model catalog");
    }
    for (const auto& entry : extra.at("models")) {
      j.at("models").push_back(entry);
    }
  }
}

static Model ParseModel(const Json& entry) {
  Keys(entry, {"id", "version", "file", "sha256", "labels", "labels_sha256",
               "task", "backend", "license", "adapter"});
  Model const m{entry.at("id"),
                entry.at("version"),
                entry.at("file"),
                entry.at("sha256"),
                entry.at("labels"),
                entry.at("labels_sha256"),
                entry.at("task"),
                entry.at("backend"),
                entry.at("license"),
                entry.value("adapter", entry.at("task").get<std::string>())};
  if (!Token(m.id) || !Token(m.version) || !Token(m.file) || !Token(m.labels) ||
      !Token(m.adapter) || m.license.empty() ||
      m.license.size() > kMaxLicenseTextBytes ||
      !std::regex_match(m.sha, std::regex("[a-f0-9]{64}")) ||
      !std::regex_match(m.labels_sha, std::regex("[a-f0-9]{64}"))) {
    Fail("invalid model metadata");
  }
  return m;
}

static void EnsureUniqueModel(const std::vector<Model>& models,
                              const Model& m) {
  for (const auto& other : models) {
    if (m.id == other.id) {
      Fail("duplicate model ID");
    }
  }
}

static std::vector<Model> Catalog(const Settings& c) {
  Json j = ReadJson(c.registry);
  MergeOptionalCatalogs(j, c);
  Keys(j, {"schema", "models"});
  if (j.at("schema") != 1 || !j.at("models").is_array() ||
      j.at("models").empty() || j.at("models").size() > kMaxModelCount) {
    Fail("invalid model registry");
  }
  std::vector<Model> models;
  for (const auto& entry : j.at("models")) {
    const auto m = ParseModel(entry);
    EnsureUniqueModel(models, m);
    models.push_back(m);
  }
  return models;
}

static void Validate(const Settings& c, const Model& m) {
  if ((m.task != "classification" && m.task != "detection") ||
      (m.backend != "cpu" && m.backend != "neutron")) {
    Fail("unsupported model task or backend");
  }
  bool const onnx =
      m.adapter == "yolox" || m.adapter == "yolov6" || m.adapter == "yolov7";
  if (onnx && m.backend != "cpu") {
    Fail("Neutron requires an i.MX95 converted TensorFlow Lite model");
  }
  if ((!onnx && m.adapter != "classification" && m.adapter != "detection" &&
       m.adapter != "yolov4-tiny") ||
      ((m.task == "classification") != (m.adapter == "classification"))) {
    Fail("model task/adapter mismatch");
  }
  if (Digest(c.assets / m.file, kMaxModelMiB * kKiB * kKiBInt, !onnx) !=
          m.sha ||
      Digest(c.assets / m.labels, kKiB * kKiBInt) != m.labels_sha) {
    Fail("model or label checksum mismatch");
  }
}

static void Persist(const fs::path& state, const Model& m) {
  std::string data = Json({{"id", m.id},
                           {"version", m.version},
                           {"sha256", m.sha},
                           {"labels_sha256", m.labels_sha}})
                         .dump() +
                     "\n";
  std::string name = (state / "active.tmp.XXXXXX").string();
  std::vector<char> temporary(name.begin(), name.end());
  temporary.push_back(0);
  int const fd = mkstemp(temporary.data());
  if (fd < 0) {
    Fail("cannot persist selection");
  }
  size_t offset = 0;
  bool ok = true;
  while (offset < data.size()) {
    ssize_t const n = write(fd, data.data() + offset, data.size() - offset);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      ok = false;
      break;
    }
    offset += static_cast<size_t>(n);
  }
  if (fsync(fd)) {
    ok = false;
  }
  if (close(fd)) {
    ok = false;
  }
  if (!ok || rename(temporary.data(), (state / "active.json").c_str())) {
    unlink(temporary.data());
    Fail("cannot commit selection");
  }
  int const directory =
      open(state.c_str(), static_cast<int>(static_cast<unsigned>(O_RDONLY) |
                                           static_cast<unsigned>(O_DIRECTORY) |
                                           static_cast<unsigned>(O_CLOEXEC)));
  if (directory >= 0) {
    fsync(directory);
    close(directory);
  }
}

struct AssetStamp {
  dev_t device;
  ino_t inode;
  off_t size;
  timespec modified, changed;

  bool operator==(const AssetStamp& other) const {
    return device == other.device && inode == other.inode &&
           size == other.size && modified.tv_sec == other.modified.tv_sec &&
           modified.tv_nsec == other.modified.tv_nsec &&
           changed.tv_sec == other.changed.tv_sec &&
           changed.tv_nsec == other.changed.tv_nsec;
  }
};

static AssetStamp Stamp(const fs::path& path) {
  struct stat st{};
  if (lstat(path.c_str(), &st) ||
      !((st.st_mode & static_cast<mode_t>(S_IFMT)) ==
        static_cast<mode_t>(S_IFREG)) ||
      st.st_size <= 0) {
    Fail("model asset unavailable or replaced by a nonregular file");
  }
  return {st.st_dev, st.st_ino, st.st_size, st.st_mtim, st.st_ctim};
}

using AssetStamps = std::pair<AssetStamp, AssetStamp>;

static AssetStamps Stamps(const Settings& c, const Model& m) {
  return {Stamp(c.assets / m.file), Stamp(c.assets / m.labels)};
}

static std::string NewInstanceId() {
  std::unique_ptr<char, decltype(&g_free)> const text(g_uuid_string_random(),
                                                      g_free);
  return text.get();
}

class Runner {
 public:
  Settings c;
  RosFrameInput frames;
  std::vector<Model> models;
  GMainLoop* loop = nullptr;
  int listener = -1;
  std::mutex mutex;
  std::thread worker;
  std::condition_variable work_ready;
  std::function<void()> pending;
  bool stopping = false;
  std::shared_ptr<ResidentWorker> resident;
  std::optional<AssetStamps> resident_assets;
  std::atomic<bool> cancel{false};
  bool busy = false;
  std::string selected;
  uint64_t model_generation = 0;
  const std::string instance_id = NewInstanceId();
  Json job = nullptr;
  uint64_t sequence = 0, last_frame_sequence = 0, last_frame_us = 0;
  Runner(const Runner&) = delete;
  Runner& operator=(const Runner&) = delete;
  Runner(Runner&&) = delete;
  Runner& operator=(Runner&&) = delete;

  explicit Runner(Settings settings)
      : c(std::move(settings)), frames(c.camera_topic), models(Catalog(c)) {
    // Linux parent-death signals follow the spawning thread. Keep this executor
    // alive for the daemon lifetime so resident interpreters survive job
    // completion.
    worker = std::thread([this]() { WorkLoop(); });
  }

  void WorkLoop() {
    std::unique_lock<std::mutex> lock(mutex);
    for (;;) {
      work_ready.wait(
          lock, [this]() { return stopping || static_cast<bool>(pending); });
      if (stopping) {
        break;
      }
      auto task = std::move(pending);
      lock.unlock();
      task();
      task = nullptr;  // Release old interpreter references before reporting
                       // idle.
      lock.lock();
      busy = false;
    }
    pending = nullptr;
    resident.reset();
  }

  ~Runner() {
    cancel = true;
    {
      std::lock_guard<std::mutex> const lock(mutex);
      stopping = true;
    }
    work_ready.notify_one();
    if (worker.joinable()) {
      worker.join();
    }
    if (listener >= 0) {
      close(listener);
      unlink(c.socket.c_str());
    }
    if (loop) {
      g_main_loop_unref(loop);
    }
  }

  [[nodiscard]] Model Find(const std::string& id) const {
    for (const auto& m : models) {
      if (m.id == id) {
        return m;
      }
    }
    Fail("unknown model ID");
    return {};
  }

  struct JobInput {
    Model model;
    fs::path image;
    bool selection = false;
    std::shared_ptr<ResidentWorker> runtime;
    bool live_frame = false;
    AssetStamps assets;
  };

  void RecordFrame(const raven::contracts::CameraFrame& frame) {
    auto now = static_cast<uint64_t>(g_get_monotonic_time());
    if (now < frame.captured_us ||
        now - frame.captured_us >
            static_cast<uint64_t>(c.frame_max_age) * kMicrosPerMillisecond) {
      Fail("inference result is stale");
    }
    uint64_t skipped =
        last_frame_sequence && frame.sequence > last_frame_sequence
            ? frame.sequence - last_frame_sequence - 1
            : 0;
    job["frame"] = {
        {"format", "rgb"},
        {"frame_id", frame.optical_frame},
        {"sequence", frame.sequence},
        {"captured_monotonic_us", frame.captured_us},
        {"captured_unix_us", frame.captured_unix_us},
        {"source_time_mapped", frame.source_time_mapped},
        {"source_pts_ns", frame.pts_ns},
        {"source_pts_valid", frame.pts_valid},
        {"width", frame.width},
        {"height", frame.height},
        {"age_at_result_ms", static_cast<double>(now - frame.captured_us) /
                                 kMicrosPerMillisecondDouble},
        {"skipped_since_last", skipped}};
    last_frame_sequence = frame.sequence;
    last_frame_us = frame.captured_us;
  }

  void RecordResult(const JobInput& input,
                    const std::shared_ptr<ResidentWorker>& runtime,
                    const Json& result) {
    const auto& m = input.model;
    const auto& validated_assets = input.assets;
    job["state"] = "succeeded";
    resident = runtime;
    resident_assets = validated_assets;
    for (const auto* field :
         {"predictions", "runtime", "backend", "worker_pid", "inference_count",
          "preprocess_ms", "inference_ms", "model_load_ms"}) {
      job[field] = result.at(field);
    }
    for (const auto* field : {"npu_partitions", "cpu_nodes"}) {
      if (result.contains(field)) {
        job[field] = result.at(field);
      }
    }
    job["task"] = m.task;
    job["adapter"] = m.adapter;
    if (m.task == "detection") {
      job["detections"] = result.at("detections");
      job["score_threshold"] = result.at("score_threshold");
      job["box_coordinates"] = result.at("box_coordinates");
    }
  }

  Json EvaluateJob(const JobInput& input,
                   const std::shared_ptr<ResidentWorker>& runtime,
                   raven::contracts::CameraFrame& frame,
                   localipc::Clock::time_point deadline) {
    const auto live_frame = input.live_frame;
    const auto& image = input.image;
    Json result;
    if (live_frame) {
      frame = frames.Latest();
      auto now = static_cast<uint64_t>(g_get_monotonic_time());
      if (frame.captured_us > now ||
          now - frame.captured_us >
              static_cast<uint64_t>(c.frame_max_age) * kMicrosPerMillisecond) {
        Fail("media frame is stale");
      }
      if (frame.captured_us <= last_frame_us) {
        Fail("media frame already consumed");
      }
      result = runtime->InferFrame(frame.data, frame.width, frame.height,
                                   deadline, cancel);
    } else {
      result = runtime->Infer(image, deadline, cancel);
    }
    return result;
  }

  void CheckJobAssets(const JobInput& input, bool& assets_changed) const {
    try {
      if (!(Stamps(c, input.model) == input.assets)) {
        Fail("model assets changed during inference");
      }
    } catch (...) {
      assets_changed = true;
      throw;
    }
  }

  void CompleteJob(const JobInput& input,
                   const std::shared_ptr<ResidentWorker>& runtime,
                   const raven::contracts::CameraFrame& frame,
                   const Json& result,
                   std::chrono::steady_clock::time_point started) {
    const auto& m = input.model;
    const auto selection = input.selection;
    const auto live_frame = input.live_frame;
    std::lock_guard<std::mutex> const lock(mutex);
    if (cancel) {
      Fail("job cancelled");
    }
    if (selection) {
      Persist(c.state, m);
      selected = m.id;
      ++model_generation;
    }
    if (live_frame) {
      RecordFrame(frame);
    }
    job["model_generation"] = model_generation;
    RecordResult(input, runtime, result);
    job["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    job["finished_us"] = g_get_real_time();
  }

  void ExecuteJob(const JobInput& input) {
    const auto& m = input.model;
    const auto selection = input.selection;
    const auto& current = input.runtime;
    bool assets_changed = false;
    try {
      auto started = std::chrono::steady_clock::now();
      auto deadline = started + std::chrono::seconds(c.timeout);
      auto runtime = current;
      if (selection || !runtime || !runtime->Ready()) {
        runtime = std::make_shared<ResidentWorker>(
            WorkerSettings{c.assets / m.file, c.assets / m.labels, m.adapter,
                           m.backend, c.threads},
            deadline, cancel);
      }
      raven::contracts::CameraFrame frame;
      const auto result = EvaluateJob(input, runtime, frame, deadline);
      CheckJobAssets(input, assets_changed);
      CompleteJob(input, runtime, frame, result, started);
    } catch (const std::exception& e) {
      std::lock_guard<std::mutex> const lock(mutex);
      if (assets_changed && !selection) {
        resident.reset();
        resident_assets.reset();
      }
      job["state"] = "failed";
      job["error"] = e.what();
      job["finished_us"] = g_get_real_time();
    }
  }

  Json Start(const Model& m, const fs::path& image, bool selection,
             bool live_frame = false) {
    if (busy) {
      Fail("worker busy; wait or cancel");
    }
    const auto validated_assets = Stamps(c, m);
    if (selection || !resident || !resident->Ready()) {
      Validate(c, m);
      if (!(Stamps(c, m) == validated_assets)) {
        Fail("model assets changed during validation");
      }
    } else if (!resident_assets || !(*resident_assets == validated_assets)) {
      resident.reset();
      resident_assets.reset();
      Fail("model assets changed; reselect the model");
    }
    cancel = false;
    busy = true;
    job = {
        {"sequence", ++sequence}, {"operation", selection ? "select" : "infer"},
        {"model", m.id},          {"model_version", m.version},
        {"model_sha256", m.sha},  {"instance_id", instance_id},
        {"state", "running"},     {"started_us", g_get_real_time()}};
    auto current = resident;
    try {
      const JobInput input{m,       image,      selection,
                           current, live_frame, validated_assets};
      pending = [this, input] { ExecuteJob(input); };
      work_ready.notify_one();
    } catch (...) {
      busy = false;
      job["state"] = "failed";
      job["error"] = "cannot start worker thread";
      throw;
    }
    return {{"ok", true}, {"accepted", true}, {"sequence", sequence}};
  }

  Json InferRequest(const Json& request) {
    std::string const image = request.at("image");
    if (!Token(image)) {
      Fail("image must be a basename in the inputs directory");
    }
    fs::path const path = c.inputs / image;
    if (fs::canonical(path).parent_path() != fs::canonical(c.inputs)) {
      Fail("image escapes input directory");
    }
    return Start(Find(selected), path, false);
  }

  Json Unload() {
    if (busy) {
      Fail("worker busy; cancel or wait before unloading");
    }
    fs::remove(c.state / "active.json");
    resident.reset();
    selected.clear();
    resident_assets.reset();
    job = nullptr;
    return {{"ok", true}};
  }

  // Dispatch holds mutex while copying this coherent model/job snapshot.
  Json Status() {
    return {{"ok", true},
            {"selected", selected},
            {"instance_id", instance_id},
            {"model_generation", model_generation},
            {"selected_model",
             selected.empty() ? Json(nullptr) : Find(selected).Describe()},
            {"busy", busy},
            {"last_job", job},
            {"resident_model", resident && resident->Ready()}};
  }

  Json Dispatch(const Json& request) {
    Keys(request, {"command", "model", "image"});
    std::lock_guard<std::mutex> const lock(mutex);
    std::string const command = request.at("command");
    if (command == "status") {
      return Status();
    }
    if (command == "list") {
      Json list = Json::array();
      for (const auto& m : models) {
        list.push_back(m.Describe());
      }
      return {{"ok", true}, {"models", list}};
    }
    if (command == "cancel") {
      cancel = true;
      return {{"ok", true}, {"busy", busy}};
    }
    if (command == "select") {
      return Start(Find(request.at("model")), c.demo, true);
    }
    if (command == "unload") {
      return Unload();
    }
    if (command == "infer-frame") {
      if (selected.empty()) {
        Fail("no model selected");
      }
      return Start(Find(selected), {}, false, true);
    }
    if (command == "infer") {
      if (selected.empty()) {
        Fail("no model selected");
      }
      return InferRequest(request);
    }
    Fail("unsupported command");
    return {};
  }

  static std::string ReadRequest(int client) {
    ucred peer{};
    socklen_t size = sizeof(peer);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &size) ||
        (peer.uid != getuid() && peer.uid != 0)) {
      Fail("unauthorized peer");
    }
    std::string request;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    bool complete = false;
    while (request.size() < kMaxRequestBytes &&
           std::chrono::steady_clock::now() < deadline) {
      pollfd input{client, POLLIN, 0};
      if (poll(&input, 1, kClientPollIntervalMs) <= 0) {
        continue;
      }
      char bytes[kRequestReadChunkBytes];
      ssize_t const n = recv(client, bytes, sizeof(bytes), MSG_DONTWAIT);
      if (n <= 0) {
        break;
      }
      request.append(bytes, static_cast<size_t>(n));
      auto newline = request.find('\n');
      if (newline != std::string::npos) {
        request.resize(newline);
        complete = true;
        break;
      }
    }
    if (!complete || request.size() >= kMaxRequestBytes) {
      Fail("incomplete or oversized request");
    }
    return request;
  }

  static gboolean Receive(gint fd, GIOCondition, gpointer data) {
    auto& r = *static_cast<Runner*>(data);
    int const client = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
      return G_SOURCE_CONTINUE;
    }
    timeval wait{1, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
    Json reply;
    try {
      const auto request = ReadRequest(client);
      reply = r.Dispatch(Json::parse(request));
    } catch (const std::exception& e) {
      reply = {{"ok", false}, {"error", e.what()}};
    }
    std::string output = reply.dump() + "\n";
    size_t offset = 0;
    while (offset < output.size()) {
      ssize_t const n = send(client, output.data() + offset,
                             output.size() - offset, MSG_NOSIGNAL);
      if (n <= 0) {
        break;
      }
      offset += static_cast<size_t>(n);
    }
    close(client);
    return G_SOURCE_CONTINUE;
  }

  static gboolean Stop(gpointer data) {
    auto& r = *static_cast<Runner*>(data);
    r.cancel = true;
    g_main_loop_quit(r.loop);
    return G_SOURCE_REMOVE;
  }

  void RestoreSelection() {
    if (!fs::exists(c.state / "active.json")) {
      return;
    }
    try {
      Json active = ReadJson(c.state / "active.json");
      Model const m = Find(active.at("id"));
      if (active.at("sha256") != m.sha || active.at("version") != m.version ||
          active.at("labels_sha256") != m.labels_sha) {
        Fail("saved model revision changed; select explicitly");
      }
      std::lock_guard<std::mutex> const lock(mutex);
      Start(m, c.demo, true);
    } catch (const std::exception& e) {
      job = {{"state", "failed"}, {"error", e.what()}};
    }
  }

  int Run() {
    fs::create_directories(c.state);
    fs::create_directories(c.inputs);
    listener = socket(AF_UNIX,
                      static_cast<int>(static_cast<unsigned>(SOCK_STREAM) |
                                       static_cast<unsigned>(SOCK_CLOEXEC) |
                                       static_cast<unsigned>(SOCK_NONBLOCK)),
                      0);
    if (listener < 0) {
      Fail("cannot create control socket");
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    localipc::SetSocketPath(address, c.socket);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) ||
        listen(listener, kListenBacklog)) {
      close(listener);
      listener = -1;
      Fail("cannot bind socket; check permissions or an existing instance");
    }
    chmod(c.socket.c_str(), kSocketPermissions);
    loop = g_main_loop_new(nullptr, FALSE);
    g_unix_fd_add(listener, G_IO_IN, Receive, this);
    g_unix_signal_add(SIGTERM, Stop, this);
    g_unix_signal_add(SIGINT, Stop, this);
    RestoreSelection();
    std::cout << "model-runner: " << models.size()
              << " registered models; isolated CPU/Neutron inference workers\n";
    constexpr auto kCameraPollMs = 10;
    const auto source = g_timeout_add(
        kCameraPollMs,
        [](gpointer data) -> gboolean {
          auto& runner = *static_cast<Runner*>(data);
          try {
            runner.frames.Spin();
          } catch (const std::exception& error) {
            std::cerr << "camera subscription failed: " << error.what() << '\n';
            g_main_loop_quit(runner.loop);
            return G_SOURCE_REMOVE;
          }
          return G_SOURCE_CONTINUE;
        },
        this);
    g_main_loop_run(loop);
    if (g_main_context_find_source_by_id(nullptr, source)) {
      g_source_remove(source);
    }
    return 0;
  }
};

static int Client(const std::string& socket_path, const Json& request) {
  if (socket_path.empty() ||
      socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
    Fail("invalid socket path");
  }
  int const fd = socket(AF_UNIX,
                        static_cast<int>(static_cast<unsigned>(SOCK_STREAM) |
                                         static_cast<unsigned>(SOCK_CLOEXEC)),
                        0);
  if (fd < 0) {
    Fail("cannot create client socket");
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  localipc::SetSocketPath(address, socket_path);
  if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
    close(fd);
    Fail("cannot connect to model-runner");
  }
  timeval wait{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
  std::string data = request.dump() + "\n";
  if (send(fd, data.data(), data.size(), MSG_NOSIGNAL) !=
      static_cast<ssize_t>(data.size())) {
    close(fd);
    Fail("command send failed");
  }
  std::string response;
  char bytes[kMaxRequestBytes];
  while (response.size() <= kMaxResponseBytes) {
    ssize_t const n = recv(fd, bytes, sizeof(bytes), 0);
    if (n <= 0) {
      break;
    }
    response.append(bytes, n);
    if (response.find('\n') != std::string::npos) {
      break;
    }
  }
  close(fd);
  Json reply = Json::parse(response);
  std::cout << reply.dump(2) << '\n';
  return reply.at("ok").get<bool>() ? 0 : 1;
}

struct Options {
  std::string config = "/etc/model-runner.conf";
  std::string socket_path = "/run/model-runner/control.sock";
  Json request;
  bool check = false;
  bool help = false;
};

static void SetOption(Options& options, const std::string& arg,
                      const std::string& value) {
  if (arg == "--config") {
    options.config = value;
  } else if (arg == "--socket") {
    options.socket_path = value;
  } else {
    options.request[arg.substr(2)] = value;
  }
}

static Options ParseOptions(int argc, char** argv) {
  Options options;

  for (int i = 1; i < argc; ++i) {
    std::string const arg = argv[i];
    if (arg == "--help") {
      std::cout
          << "model-runner [--config FILE] [--check]\nmodel-runner "
             "--command list|status|select|unload|infer|infer-frame|cancel "
             "[--model ID] [--image BASENAME] [--socket PATH]\n";
      options.help = true;
      return options;
    }
    if (arg == "--check") {
      options.check = true;
    } else if ((arg == "--config" || arg == "--socket" || arg == "--command" ||
                arg == "--model" || arg == "--image") &&
               i + 1 < argc) {
      std::string const value = argv[++i];
      SetOption(options, arg, value);
    } else {
      Fail("invalid argument; use --help");
    }
  }
  return options;
}

static void ValidateCatalog(const Settings& c) {
  for (const auto& m : Catalog(c)) {
    Validate(c, m);
  }
}

int main(int argc, char** argv) {
  try {
    std::signal(SIGPIPE, SIG_IGN);
    if (argc == kWorkerArgumentCount &&
        std::string(argv[1]) == "--inference-worker") {
      return InferenceWorker(argv[2], argv[3], argv[4],
                             argv[kWorkerBackendArgumentIndex],
                             std::stoi(argv[kWorkerThreadsArgumentIndex]));
    }
    const auto options = ParseOptions(argc, argv);
    if (options.help) {
      return 0;
    }
    const auto& request = options.request;
    const auto& socket_path = options.socket_path;
    const auto& config = options.config;
    const auto check = options.check;
    if (request.contains("command")) {
      return Client(socket_path, request);
    }
    // Startup only, before Runner creates any worker threads.
    // NOLINTBEGIN(concurrency-mt-unsafe)
    if (setenv("OPENBLAS_NUM_THREADS", "1", 1) != 0 ||
        setenv("OMP_NUM_THREADS", "2", 1) != 0) {
      Fail("cannot set worker thread limits");
    }
    // NOLINTEND(concurrency-mt-unsafe)
    umask(kPrivateFileUmask);
    Settings c(ReadJson(config));
    if (check) {
      ValidateCatalog(c);
      std::cout << "Registry and asset hashes valid; select "
                   "performs runtime inference validation\n";
      return 0;
    }
    rclcpp::init(argc, argv, rclcpp::InitOptions{},
                 rclcpp::SignalHandlerOptions::None);
    int result = 0;
    {
      Runner runner(std::move(c));
      result = runner.Run();
    }
    rclcpp::shutdown();
    return result;
  } catch (const std::exception& e) {
    std::cerr << "model-runner: " << e.what() << '\n';
    return 1;
  }
}
