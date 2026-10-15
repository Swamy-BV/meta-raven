// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "../core/result.h"

namespace raven::perception {
struct Snapshot {
  std::optional<Identity> identity;
  std::vector<std::string> labels;
  std::optional<Result> result;
  std::string task, health;
  bool failed = false;
  float threshold = 0;
};

class ModelBackend {
 public:
  ModelBackend() = default;
  virtual ~ModelBackend() = default;
  ModelBackend(const ModelBackend&) = delete;
  ModelBackend& operator=(const ModelBackend&) = delete;
  ModelBackend(ModelBackend&&) = delete;
  ModelBackend& operator=(ModelBackend&&) = delete;
  virtual Snapshot Poll() = 0;
};
}  // namespace raven::perception
