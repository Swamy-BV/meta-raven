// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rgb_input.h"
#include "worker_frame_buffer.h"

class InferenceModel {
 public:
  InferenceModel() = default;
  InferenceModel(const InferenceModel&) = delete;
  InferenceModel& operator=(const InferenceModel&) = delete;
  InferenceModel(InferenceModel&&) = delete;
  InferenceModel& operator=(InferenceModel&&) = delete;
  virtual ~InferenceModel() = default;
  virtual nlohmann::json Infer(const std::filesystem::path& image) = 0;
  virtual nlohmann::json Infer(const RgbLayout& image) = 0;
};

std::vector<uint8_t> LoadRgbImage(const std::filesystem::path& image,
                                  int& width, int& height);
std::unique_ptr<InferenceModel> MakeOnnxModel(const char* model,
                                              const char* labels,
                                              const char* adapter, int threads);

struct WorkerSettings {
  std::filesystem::path model;
  std::filesystem::path labels;
  std::string task;
  std::string backend;
  int threads = 1;
};

// A persistent, isolated interpreter. Only the job thread performs exchanges.
class ResidentWorker {
 public:
  ResidentWorker(const WorkerSettings& settings,
                 std::chrono::steady_clock::time_point deadline,
                 std::atomic<bool>& cancel);
  ~ResidentWorker();
  ResidentWorker(const ResidentWorker&) = delete;
  ResidentWorker& operator=(const ResidentWorker&) = delete;
  ResidentWorker(ResidentWorker&&) = delete;
  ResidentWorker& operator=(ResidentWorker&&) = delete;
  nlohmann::json Infer(const std::filesystem::path& image,
                       std::chrono::steady_clock::time_point deadline,
                       std::atomic<bool>& cancel);
  nlohmann::json InferFrame(const RgbLayout& image,
                            std::chrono::steady_clock::time_point deadline,
                            std::atomic<bool>& cancel);

  [[nodiscard]] bool Ready() const { return healthy_.load(); }

 private:
  WorkerFrameBuffer frames_;
  pid_t pid_ = -1;
  int input_ = -1, output_ = -1;
  std::string buffered_;
  std::atomic<bool> healthy_{false};
  void Terminate();
  void SendChunk(const std::string& message, size_t& sent) const;
  void ReadChunk();
  nlohmann::json DecodeResponse(size_t end);
  nlohmann::json ExchangeLoop(const std::string& message,
                              std::chrono::steady_clock::time_point deadline,
                              std::atomic<bool>& cancel);
  nlohmann::json Exchange(const nlohmann::json* request,
                          std::chrono::steady_clock::time_point deadline,
                          std::atomic<bool>& cancel);
};

int InferenceWorker(const char* model, const char* labels, const char* task,
                    const char* backend, int threads);
