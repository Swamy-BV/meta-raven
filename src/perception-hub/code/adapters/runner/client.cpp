// SPDX-License-Identifier: AGPL-3.0-only
#include "client.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glib.h>
#include <nlohmann/json.hpp>

#include "../../core/result.h"
#include "../../ports/model_backend.h"
#include "decode.h"
#include <local_ipc.h>

namespace raven::perception {
namespace {
constexpr auto kTimeoutMs = 200;
constexpr auto kMaxResponse = 65536;
constexpr auto kChunkBytes = 4096;
constexpr std::size_t kMaxClasses = 4096;
constexpr std::size_t kMaxLabelBytes = 256;
constexpr auto kPollWaitMs = 5;
constexpr auto kRunnerUid = 971;

class Descriptor {
 public:
  explicit Descriptor(int value) : value_(value) {
    localipc::Check(value >= 0, "cannot open model socket");
  }

  ~Descriptor() { close(value_); }

  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;

  [[nodiscard]] int Get() const { return value_; }

 private:
  int value_;
};

std::string ReadReply(int fd, localipc::Clock::time_point deadline) {
  std::string reply;
  while (reply.size() < kMaxResponse) {
    localipc::Check(localipc::Clock::now() < deadline, "model reply timeout");
    char bytes[kChunkBytes];
    const auto count = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
      pollfd input{fd, POLLIN, 0};
      localipc::Check(poll(&input, 1, kPollWaitMs) >= 0 || errno == EINTR,
                      "model socket poll failed");
      continue;
    }
    localipc::Check(count > 0, "model socket disconnected");
    reply.append(bytes, static_cast<std::size_t>(count));
    localipc::Check(reply.size() <= kMaxResponse, "model reply too large");
    const auto end = reply.find('\n');
    if (end != std::string::npos) {
      return reply.substr(0, end);
    }
  }
  throw std::runtime_error("model reply too large");
}

std::uint64_t MonotonicUs() {
  return static_cast<std::uint64_t>(g_get_monotonic_time());
}
}  // namespace

RunnerBackend::RunnerBackend(std::string socket, std::string assets,
                             bool automatic, std::uint64_t max_age_us)
    : socket_(std::move(socket)),
      assets_(std::move(assets)),
      automatic_(automatic),
      max_age_us_(max_age_us) {}

nlohmann::json RunnerBackend::Request(const std::string& command) const {
  const Descriptor fd(
      socket(AF_UNIX,
             static_cast<int>(static_cast<unsigned>(SOCK_STREAM) |
                              static_cast<unsigned>(SOCK_CLOEXEC) |
                              static_cast<unsigned>(SOCK_NONBLOCK)),
             0));
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  localipc::SetSocketPath(address, socket_);
  const auto deadline =
      localipc::Clock::now() + std::chrono::milliseconds(kTimeoutMs);
  const int connected =
      connect(fd.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address));
  localipc::Check(connected == 0, "model-runner unavailable or socket busy");
  ucred peer{};
  socklen_t size = sizeof(peer);
  localipc::Check(
      getsockopt(fd.Get(), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 &&
          peer.uid == kRunnerUid,
      "unexpected model-runner identity");
  const auto text = nlohmann::json{{"command", command}}.dump() + '\n';
  localipc::SendBytes(fd.Get(), text.data(), text.size(), deadline);
  auto reply = nlohmann::json::parse(ReadReply(fd.Get(), deadline));
  localipc::Check(reply.at("ok").get<bool>(),
                  reply.value("error", "model request rejected").c_str());
  return reply;
}

void RunnerBackend::UpdateLabels(const nlohmann::json& status,
                                 const Identity& identity) {
  const auto key =
      identity.instance + ':' + std::to_string(identity.generation);
  if (key == metadata_key_) {
    return;
  }
  const auto& model = status.at("selected_model");
  const auto file = model.at("labels").get<std::string>();
  localipc::Check(std::filesystem::path(file).filename() == file &&
                      file != "." && file != "..",
                  "invalid labels filename");
  std::ifstream stream(std::filesystem::path(assets_) / file);
  localipc::Check(stream.good(), "labels unavailable");
  localipc::Check(
      std::filesystem::file_size(std::filesystem::path(assets_) / file) <
          kMaxClasses * (kMaxLabelBytes + 1),
      "labels exceed byte limit");
  const std::string content((std::istreambuf_iterator<char>(stream)), {});
  std::istringstream lines(content);
  std::vector<std::string> labels;
  std::string line;
  while (std::getline(lines, line)) {
    localipc::Check(
        labels.size() < kMaxClasses && line.size() <= kMaxLabelBytes,
        "labels exceed limits");
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    localipc::Check(!line.empty(), "empty class label");
    labels.push_back(line);
  }
  localipc::Check(lines.eof() && !labels.empty(), "invalid labels file");
  std::unique_ptr<char, decltype(&g_free)> const sha(
      g_compute_checksum_for_data(
          G_CHECKSUM_SHA256, reinterpret_cast<const guchar*>(content.data()),
          content.size()),
      g_free);
  localipc::Check(model.at("labels_sha256") == sha.get(),
                  "labels checksum mismatch");
  labels_ = std::move(labels);
  metadata_key_ = key;
}

Snapshot RunnerBackend::Poll() {
  const auto status = Request("status");
  Snapshot snapshot;
  if (status.at("selected").get<std::string>().empty()) {
    snapshot.health = "No model selected";
    return snapshot;
  }
  snapshot.identity = ActiveIdentity(status);
  UpdateLabels(status, *snapshot.identity);
  snapshot.labels = labels_;
  snapshot.task = status.at("selected_model").at("task");
  snapshot.result = Decode(status, MonotonicUs(), max_age_us_);
  const auto& job = status.at("last_job");
  snapshot.failed = !job.is_null() && job.at("state") == "failed";
  snapshot.health = snapshot.result ? "Fresh inference output"
                                    : "Waiting for fresh live result";
  if (snapshot.failed) {
    snapshot.health = job.value("error", "Inference failed");
  }
  if (!job.is_null()) {
    snapshot.threshold = job.value("score_threshold", 0.0F);
  }
  if (automatic_ && MayInfer(status)) {
    const auto accepted = Request("infer-frame");
    localipc::Check(accepted.at("accepted").get<bool>(),
                    "frame request was not accepted");
  }
  return snapshot;
}
}  // namespace raven::perception
