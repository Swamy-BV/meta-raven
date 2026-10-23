// SPDX-License-Identifier: AGPL-3.0-only
#include "ground_projector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace raven::tracking {
namespace {
constexpr std::size_t kPoseSamples = 256;
constexpr std::int64_t kMaximumSpan = 100000000;
constexpr double kMinimumDown = 0.05;
constexpr double kMinimumRange = 0.2;
constexpr double kNormTolerance = 0.01;

template <class Samples, class Blend>
auto Interpolate(const Samples& samples, std::int64_t stamp, Blend blend)
    -> std::optional<typename Samples::value_type::second_type> {
  const auto after = std::lower_bound(
      samples.begin(), samples.end(), stamp,
      [](const auto& sample, auto time) { return sample.first < time; });
  if (after == samples.end()) {
    return std::nullopt;
  }
  if (after->first == stamp) {
    return after->second;
  }
  if (after == samples.begin()) {
    return std::nullopt;
  }
  const auto before = std::prev(after);
  const auto span = after->first - before->first;
  if (span <= 0 || span > kMaximumSpan) {
    return std::nullopt;
  }
  return blend(
      before->second, after->second,
      static_cast<double>(stamp - before->first) / static_cast<double>(span));
}

template <class Samples, class Value>
void Append(Samples& samples, std::int64_t stamp, const Value& value) {
  if (stamp <= 0 || (!samples.empty() && stamp <= samples.back().first)) {
    return;
  }
  samples.emplace_back(stamp, value);
  while (samples.size() > kPoseSamples) {
    samples.pop_front();
  }
}

Eigen::Quaterniond Quaternion(const std::array<double, 4>& value) {
  return {value[0], value[1], value[2], value[3]};
}
}  // namespace

GroundProjector::GroundProjector(GroundCamera camera) : camera_(camera) {
  if (!std::isfinite(camera.ground_z) || !std::isfinite(camera.pitch) ||
      !std::isfinite(camera.max_range) || camera.max_range <= 0 ||
      !std::all_of(camera.offset.begin(), camera.offset.end(),
                   [](double value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Invalid ground projection configuration");
  }
}

bool GroundProjector::Calibrate(const GroundCalibration& value) {
  if (value.frame.empty() || value.width == 0 || value.height == 0 ||
      !std::isfinite(value.fx) || !std::isfinite(value.fy) ||
      !std::isfinite(value.cx) || !std::isfinite(value.cy) || value.fx <= 0 ||
      value.fy <= 0) {
    throw std::invalid_argument("Invalid ground camera calibration");
  }
  const bool changed =
      calibration_ &&
      (calibration_->frame != value.frame ||
       calibration_->width != value.width ||
       calibration_->height != value.height || calibration_->fx != value.fx ||
       calibration_->fy != value.fy || calibration_->cx != value.cx ||
       calibration_->cy != value.cy);
  calibration_ = value;
  return changed;
}

void GroundProjector::Position(std::int64_t stamp,
                               const std::array<double, 3>& position) {
  if (std::all_of(position.begin(), position.end(),
                  [](double value) { return std::isfinite(value); })) {
    Append(positions_, stamp, position);
  }
}

void GroundProjector::Attitude(std::int64_t stamp,
                               const std::array<double, 4>& orientation) {
  auto q = Quaternion(orientation);
  if (!std::isfinite(q.squaredNorm()) ||
      std::abs(q.squaredNorm() - 1) > kNormTolerance) {
    return;
  }
  q.normalize();
  Append(attitudes_, stamp, std::array<double, 4>{q.w(), q.x(), q.y(), q.z()});
}

std::optional<GroundPoint> GroundProjector::Project(const Box& box,
                                                    const std::string& frame,
                                                    std::int64_t stamp) const {
  if (!calibration_ || calibration_->frame != frame) {
    return std::nullopt;
  }
  const auto position = Interpolate(
      positions_, stamp,
      [](const auto& first, const auto& last, double fraction) {
        std::array<double, 3> result{};
        for (std::size_t axis = 0; axis < result.size(); ++axis) {
          result[axis] = first[axis] + fraction * (last[axis] - first[axis]);
        }
        return result;
      });
  const auto attitude = Interpolate(
      attitudes_, stamp,
      [](const auto& first, const auto& last, double fraction) {
        const auto q = Quaternion(first).slerp(fraction, Quaternion(last));
        return std::array<double, 4>{q.w(), q.x(), q.y(), q.z()};
      });
  const auto foot = box.center_y + box.height / 2;
  if (!position || !attitude || box.center_x < 0 || foot < 0 ||
      box.center_x >= calibration_->width || foot >= calibration_->height) {
    return std::nullopt;
  }
  const auto right = (box.center_x - calibration_->cx) / calibration_->fx;
  const auto down = (foot - calibration_->cy) / calibration_->fy;
  const auto sine = std::sin(camera_.pitch);
  const auto cosine = std::cos(camera_.pitch);
  const Eigen::Vector3d body_ray(cosine - sine * down, right,
                                 sine + cosine * down);
  const auto body = Quaternion(*attitude);
  const auto yaw =
      std::atan2(2 * (body.w() * body.z() + body.x() * body.y()),
                 1 - 2 * (body.y() * body.y() + body.z() * body.z()));
  const Eigen::Quaterniond orientation =
      camera_.stabilized
          ? Eigen::Quaterniond(Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ()))
          : body;
  const Eigen::Vector3d ray = orientation * body_ray;
  const Eigen::Vector3d offset(camera_.offset[0], camera_.offset[1],
                               camera_.offset[2]);
  const Eigen::Vector3d location((*position)[0], (*position)[1],
                                 (*position)[2]);
  const Eigen::Vector3d origin = location + body * offset;
  if (ray.z() <= kMinimumDown || camera_.ground_z <= origin.z()) {
    return std::nullopt;
  }
  const Eigen::Vector3d point =
      origin + ray * ((camera_.ground_z - origin.z()) / ray.z());
  const auto range = (point - location).head<2>().norm();
  if (!point.allFinite() || range < kMinimumRange ||
      range > camera_.max_range) {
    return std::nullopt;
  }
  return GroundPoint{point.x(), point.y()};
}

void GroundProjector::InvalidateCalibration() {
  calibration_.reset();
  Reset();
}

void GroundProjector::Reset() {
  positions_.clear();
  attitudes_.clear();
}
}  // namespace raven::tracking
