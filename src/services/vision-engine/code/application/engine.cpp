// SPDX-License-Identifier: AGPL-3.0-only
#include "engine.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <glib.h>
#include <nlohmann/json.hpp>

#include "../core/decode.h"
#include "../core/result.h"
#include "../inference_worker.h"
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
constexpr auto kDigestChunkBytes = 65536;
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
  fs::path registry, assets, demo, state;

  int frame_max_age;
  int threads, timeout;

  explicit Settings(const Json& j) {
    Keys(j, {"schema", "registry", "assets", "demo", "state", "threads",
             "timeout_seconds", "frame_max_age_ms"});
    if (j.at("schema") != 1) {
      Fail("unsupported configuration schema");
    }
    registry = j.at("registry").get<std::string>();
    assets = j.at("assets").get<std::string>();
    demo = j.at("demo").get<std::string>();
    state = j.at("state").get<std::string>();
    frame_max_age = j.value("frame_max_age_ms", kDefaultFrameAgeMs);
    if (frame_max_age < kMinFrameAgeMs || frame_max_age > kMaxFrameAgeMs) {
      Fail("invalid frame age");
    }
    threads = j.at("threads");
    timeout = j.at("timeout_seconds");
    if (threads < 1 || threads > 4 || timeout < 1 ||
        timeout > kMaxJobTimeoutSeconds) {
      Fail("configuration outside supported bounds");
    }
    for (const auto& path : {registry, assets, demo, state}) {
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

namespace {
class Runner {
 public:
  Settings c;
  raven::vision::FrameProvider frames;
  std::vector<Model> models;
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

  explicit Runner(Settings settings, raven::vision::FrameProvider provider)
      : c(std::move(settings)),
        frames(std::move(provider)),
        models(Catalog(c)) {
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
        {"source_stamp_ns", frame.source_stamp_ns},
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
                   std::chrono::steady_clock::time_point deadline) {
    const auto live_frame = input.live_frame;
    const auto& image = input.image;
    Json result;
    if (live_frame) {
      frame = frames();
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

  // The caller holds mutex while copying this coherent model/job snapshot.
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
};
}  // namespace

namespace {
void AddMetrics(raven::vision::Snapshot& snapshot, const Json& job) {
  if (job.is_null()) {
    return;
  }
  snapshot.threshold = job.value("score_threshold", 0.0F);
  for (const auto* key : {"preprocess_ms", "inference_ms", "model_load_ms",
                          "npu_partitions", "cpu_nodes"}) {
    if (job.contains(key)) {
      snapshot.metrics.emplace_back(key, job.at(key).dump());
    }
  }
}
}  // namespace

namespace raven::vision {
struct Engine::Impl {
  Runner runner;
  std::uint64_t max_age_us;
  std::string metadata_key, restore_error;
  std::vector<std::string> labels;

  Impl(const std::string& config, FrameProvider frames, std::uint64_t age)
      : runner(Settings(ReadJson(config)), std::move(frames)), max_age_us(age) {
    fs::create_directories(runner.c.state);
  }

  Json Status() {
    std::lock_guard<std::mutex> const lock(runner.mutex);
    return runner.Status();
  }

  void UpdateLabels(const Json& status, const Identity& identity) {
    const auto key =
        identity.instance + ':' + std::to_string(identity.generation);
    if (key == metadata_key) {
      return;
    }
    const auto model = runner.Find(status.at("selected"));
    const auto path = runner.c.assets / model.labels;
    if (Digest(path, kKiB * kKiBInt) != model.labels_sha) {
      Fail("labels checksum mismatch");
    }
    std::ifstream stream(path);
    std::vector<std::string> next;
    std::string line;
    constexpr auto kMaxLabelBytes = 256;
    constexpr auto kMaxClasses = 4096;
    while (std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty() || line.size() > kMaxLabelBytes ||
          next.size() >= kMaxClasses) {
        Fail("invalid or oversized class label");
      }
      next.push_back(line);
    }
    if (!stream.eof() || next.empty()) {
      Fail("labels unavailable");
    }
    labels = std::move(next);
    metadata_key = key;
  }
};

Engine::Engine(const std::string& config, FrameProvider frames,
               std::uint64_t max_age_us)
    : impl_(std::make_unique<Impl>(config, std::move(frames), max_age_us)) {}

Engine::~Engine() = default;

std::vector<std::string> Engine::Models() const {
  std::vector<std::string> ids;
  for (const auto& model : impl_->runner.models) {
    ids.push_back(model.id);
  }
  return ids;
}

std::string Engine::InitialModel(const std::string& fallback) {
  const auto& runner = impl_->runner;
  const auto path = runner.c.state / "active.json";
  if (!fs::exists(path)) {
    return fallback;
  }
  try {
    const auto active = ReadJson(path);
    const auto model = runner.Find(active.at("id"));
    if (active.at("sha256") != model.sha ||
        active.at("version") != model.version ||
        active.at("labels_sha256") != model.labels_sha) {
      Fail(
          "saved model revision changed; explicitly select or unload the "
          "model");
    }
    return model.id;
  } catch (const std::exception& error) {
    // Stay available for an explicit ROS selection instead of restarting or
    // silently loading a different model after an incompatible saved selection.
    impl_->restore_error = error.what();
    return {};
  }
}

void Engine::Select(const std::string& id) {
  auto& runner = impl_->runner;
  std::lock_guard<std::mutex> const lock(runner.mutex);
  if (id.empty()) {
    runner.Unload();
  } else {
    runner.Start(runner.Find(id), runner.c.demo, true);
  }
  impl_->restore_error.clear();
}

void Engine::Cancel() { impl_->runner.cancel = true; }

void Engine::Infer() {
  auto& runner = impl_->runner;
  std::lock_guard<std::mutex> const lock(runner.mutex);
  if (runner.selected.empty()) {
    Fail("no model selected");
  }
  runner.Start(runner.Find(runner.selected), {}, false, true);
}

Snapshot Engine::Poll(bool automatic) {
  const auto status = impl_->Status();
  Snapshot snapshot;
  const auto& job = status.at("last_job");
  snapshot.failed = !impl_->restore_error.empty() ||
                    (!job.is_null() && job.at("state") == "failed");
  if (status.at("selected").get<std::string>().empty()) {
    snapshot.health = "Waiting for model selection";
    if (!impl_->restore_error.empty()) {
      snapshot.health = impl_->restore_error;
    } else if (snapshot.failed) {
      snapshot.health = job.value("error", "Model selection failed");
    }
    return snapshot;
  }
  snapshot.identity = ActiveIdentity(status);
  impl_->UpdateLabels(status, *snapshot.identity);
  snapshot.labels = impl_->labels;
  snapshot.task = status.at("selected_model").at("task");
  snapshot.result =
      Decode(status, static_cast<std::uint64_t>(g_get_monotonic_time()),
             impl_->max_age_us);
  snapshot.health = snapshot.result ? "Fresh inference output"
                                    : "Waiting for fresh camera result";
  if (snapshot.failed) {
    snapshot.health = job.value("error", "Inference failed");
  }
  AddMetrics(snapshot, job);
  snapshot.metrics.emplace_back("model", snapshot.identity->model);
  snapshot.metrics.emplace_back(
      "backend", status.at("selected_model").at("backend").get<std::string>());
  snapshot.metrics.emplace_back("busy", status.at("busy").dump());
  if (automatic && MayInfer(status)) {
    Infer();
  }
  return snapshot;
}
}  // namespace raven::vision
