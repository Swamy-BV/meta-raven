// SPDX-License-Identifier: AGPL-3.0-only
#include "decode.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "result.h"

namespace raven::vision {
namespace {
constexpr auto kNanosPerMicro = 1000ULL;
constexpr auto kMaxDimension = 16384;
constexpr auto kMaxClasses = 4096;
constexpr auto kMaxDetections = 100;
constexpr auto kShaLength = 64;
constexpr auto kMaxRosStampUs = 2147483647000000ULL;

void Require(bool condition, const char* reason) {
  if (!condition) {
    throw std::runtime_error(reason);
  }
}

Hypothesis ReadHypothesis(const nlohmann::json& value) {
  const auto id = value.at("class_id").get<int>();
  const auto score = value.at("score").get<double>();
  Require(id >= 0 && id < kMaxClasses && std::isfinite(score) && score >= 0 &&
              score <= 1,
          "invalid class or confidence");
  return {static_cast<std::uint16_t>(id), score};
}

Result ReadFrame(const nlohmann::json& job, const Identity& identity) {
  Result result;
  result.identity = identity;
  result.job_sequence = job.at("sequence");
  const auto& frame = job.at("frame");
  result.frame_sequence = frame.at("sequence");
  result.optical_frame = frame.at("frame_id").get<std::string>();
  result.stamp_us = frame.at("captured_unix_us");
  result.stamp_ns =
      frame.value("source_stamp_ns", result.stamp_us * kNanosPerMicro);
  // ROS source time may be simulated. Freshness uses the independent monotonic
  // capture timestamp; do not require ROS and Unix epochs to be equal.
  Require(
      result.stamp_ns > 0 && result.stamp_ns / kNanosPerMicro <= kMaxRosStampUs,
      "invalid source clock timestamp");
  result.captured_monotonic_us = frame.at("captured_monotonic_us");
  result.pts_ns = frame.at("source_pts_ns");
  result.pts_valid = frame.at("source_pts_valid");
  result.source_time_mapped = frame.at("source_time_mapped");
  result.width = frame.at("width");
  result.height = frame.at("height");
  result.task = job.at("task");
  Require(result.stamp_us > 0 && result.stamp_us <= kMaxRosStampUs &&
              result.frame_sequence > 0 && result.job_sequence > 0 &&
              !result.optical_frame.empty() &&
              result.optical_frame.front() != '/' && result.width > 0 &&
              result.width <= kMaxDimension && result.height > 0 &&
              result.height <= kMaxDimension,
          "invalid frame geometry or timestamp");
  return result;
}

Detection ReadBox(const nlohmann::json& value, const Result& result) {
  const auto& box = value.at("box");
  const double xmin = box.at("xmin");
  const double ymin = box.at("ymin");
  const double xmax = box.at("xmax");
  const double ymax = box.at("ymax");
  Require(std::isfinite(xmin) && std::isfinite(ymin) && std::isfinite(xmax) &&
              std::isfinite(ymax) && xmin >= 0 && ymin >= 0 && xmax <= 1 &&
              ymax <= 1 && xmax > xmin && ymax > ymin,
          "invalid normalized box");
  return {ReadHypothesis(value), (xmin + xmax) * result.width / 2,
          (ymin + ymax) * result.height / 2, (xmax - xmin) * result.width,
          (ymax - ymin) * result.height};
}

void ReadOutputs(Result& result, const nlohmann::json& job) {
  if (result.task == "classification") {
    const auto& values = job.at("predictions");
    Require(values.is_array() && values.size() <= kMaxClasses,
            "invalid classification count");
    for (const auto& value : values) {
      result.classification.push_back(ReadHypothesis(value));
    }
  } else if (result.task == "detection") {
    Require(job.at("box_coordinates") == "normalized-source-xyxy",
            "unsupported box coordinates");
    const auto& values = job.at("detections");
    Require(values.is_array() && values.size() <= kMaxDetections,
            "invalid detection count");
    for (const auto& value : values) {
      result.detections.push_back(ReadBox(value, result));
    }
  } else {
    throw std::runtime_error("unsupported inference task");
  }
}
}  // namespace

bool MayInfer(const nlohmann::json& status) {
  if (status.at("busy").get<bool>() ||
      status.at("selected").get<std::string>().empty()) {
    return false;
  }
  const auto& job = status.at("last_job");
  return job.is_null() || job.at("state") != "failed" ||
         job.value("operation", "") != "select";
}

Identity ActiveIdentity(const nlohmann::json& status) {
  const auto& model = status.at("selected_model");
  Identity identity{status.at("instance_id"), model.at("id"),
                    model.at("version"), model.at("sha256"),
                    status.at("model_generation")};
  Require(!identity.instance.empty() && !identity.model.empty() &&
              !identity.version.empty() && identity.sha.size() == kShaLength &&
              identity.generation > 0,
          "invalid model identity");
  return identity;
}

std::optional<Result> Decode(const nlohmann::json& status,
                             std::uint64_t now_monotonic_us,
                             std::uint64_t max_age_us) {
  Require(status.at("ok").get<bool>(), "inference status unavailable");
  const auto& job = status.at("last_job");
  if (status.at("selected").get<std::string>().empty() || job.is_null() ||
      job.at("state") != "succeeded" || job.at("operation") != "infer" ||
      !job.contains("frame")) {
    return std::nullopt;
  }
  const auto identity = ActiveIdentity(status);
  if (job.at("instance_id") != identity.instance ||
      job.at("model_generation") != identity.generation ||
      job.at("model") != identity.model ||
      job.at("model_version") != identity.version ||
      job.at("model_sha256") != identity.sha) {
    return std::nullopt;
  }
  auto result = ReadFrame(job, identity);
  if (now_monotonic_us < result.captured_monotonic_us || max_age_us == 0 ||
      now_monotonic_us - result.captured_monotonic_us >= max_age_us) {
    return std::nullopt;
  }
  ReadOutputs(result, job);
  return result;
}
}  // namespace raven::vision
