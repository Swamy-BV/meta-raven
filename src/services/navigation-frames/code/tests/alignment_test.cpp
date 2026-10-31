// SPDX-License-Identifier: AGPL-3.0-only
#include "core/alignment.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include <Eigen/Geometry>

namespace {
using raven::navigation::FrameAlignment;
using raven::navigation::PoseHistory;
using raven::navigation::TimedPose;

void Require(const bool valid, const std::string& reason) {
  if (!valid) {
    throw std::runtime_error(reason);
  }
}

TimedPose Pose(const std::int64_t stamp, const double x, const double yaw = 0) {
  TimedPose pose;
  pose.stamp_ns = stamp;
  pose.world_from_body.translation().x() = x;
  pose.world_from_body.linear() =
      Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ()).toRotationMatrix();
  return pose;
}

void Interpolation() {
  PoseHistory history(4, 100);
  Require(history.Push(Pose(100, 0)), "First sample rejected");
  Require(history.Push(Pose(200, 2, 1)), "Second sample rejected");
  const auto middle = history.At(150);
  Require(middle.has_value(), "Bracketed sample missing");
  Require(std::abs(middle->world_from_body.translation().x() - 1) < 1e-9,
          "Translation is not acquisition-time interpolated");
  Require(std::abs(Eigen::AngleAxisd(middle->world_from_body.linear()).angle() -
                   0.5) < 1e-9,
          "Rotation is not spherically interpolated");
}

void Bounds() {
  PoseHistory history(2, 50);
  Require(history.Push(Pose(100, 0)), "First sample rejected");
  Require(history.Push(Pose(200, 1)), "Second sample rejected");
  Require(!history.At(99) && !history.At(201), "History extrapolated");
  Require(!history.At(150), "Interpolation crossed an excessive gap");
  Require(history.At(200).has_value(), "Exact endpoint rejected");
  Require(history.Push(Pose(250, 2)), "Third sample rejected");
  Require(!history.At(100), "Capacity failed to evict old samples");
  history.Clear();
  Require(!history.At(250), "Reset retained old samples");
}

void Invalid() {
  Require(!raven::navigation::AcquisitionNanoseconds(0),
          "Zero acquisition timestamp accepted");
  Require(!raven::navigation::AcquisitionNanoseconds(
              std::numeric_limits<std::uint64_t>::max()),
          "Microsecond-to-nanosecond conversion overflowed");
  Require(raven::navigation::AcquisitionNanoseconds(42).value() == 42000,
          "Acquisition units changed");
  PoseHistory history(4, 100);
  Require(!history.Push(Pose(0, 0)), "Zero timestamp accepted");
  Require(history.Push(Pose(100, 0)), "Valid pose rejected");
  Require(!history.Push(Pose(100, 1)), "Duplicate timestamp accepted");
  Require(!history.Push(Pose(99, 1)), "Regressing timestamp accepted");
  auto invalid = Pose(200, std::numeric_limits<double>::quiet_NaN());
  Require(!history.Push(invalid), "Nonfinite translation accepted");
  invalid = Pose(200, 0);
  invalid.world_from_body.linear()(0, 0) = -1;
  Require(!history.Push(invalid), "Reflection accepted as a rotation");
}

void Freshness() {
  constexpr std::int64_t kNow = 1000000000;
  constexpr std::int64_t kAge = 200000000;
  constexpr std::int64_t kFuture = 5000000;
  using raven::navigation::FreshAcquisition;
  Require(FreshAcquisition(kNow - kAge, kNow, kAge, kFuture),
          "Age boundary rejected");
  Require(!FreshAcquisition(kNow - kAge - 1, kNow, kAge, kFuture),
          "Stale sample accepted");
  Require(FreshAcquisition(kNow + kFuture, kNow, kAge, kFuture),
          "Future tolerance boundary rejected");
  Require(!FreshAcquisition(kNow + kFuture + 1, kNow, kAge, kFuture),
          "Future sample accepted");
  Require(!FreshAcquisition(0, kNow, kAge, kFuture),
          "Uninitialized stamp accepted");
  Require(!FreshAcquisition(kNow, 0, kAge, kFuture),
          "Uninitialized clock accepted");
  Require(!FreshAcquisition(kNow, 1700000000000000000LL, kAge, kFuture),
          "Different acquisition and ROS epochs accepted");
}

void Conversion() {
  auto input = Pose(1, 1);
  input.world_from_body.translation() = Eigen::Vector3d(1, 2, -3);
  const auto output = raven::navigation::NedFrdToEnuFlu(input.world_from_body);
  Require(output.translation().isApprox(Eigen::Vector3d(2, 1, 3)),
          "NED position did not convert to ENU");
  Require((output.linear() * Eigen::Vector3d::UnitX())
              .isApprox(Eigen::Vector3d::UnitY()),
          "North-facing aircraft lost its heading");
  Require((output.linear() * Eigen::Vector3d::UnitZ())
              .isApprox(Eigen::Vector3d::UnitZ()),
          "Body FLU up does not point up");
}

void Alignment() {
  FrameAlignment alignment(0.2, 0.1);
  const auto local = Pose(100, 2, 0.3);
  const auto reference = Pose(100, 10, 0.8);
  Require(alignment.Observe(reference, local, 0), "Initial pairing failed");
  Require((alignment.Transform().value() * local.world_from_body)
              .matrix()
              .isApprox(reference.world_from_body.matrix()),
          "World alignment lost translation or yaw");
  Require(!alignment.Observe(Pose(101, 10), local, 0),
          "Different acquisition times paired");
}

void Reset() {
  FrameAlignment alignment(0.2, 0.1);
  Require(alignment.Observe(Pose(100, 0), Pose(100, 0), 0), "Init failed");
  Require(!alignment.Observe(Pose(200, 0), Pose(200, 0), 1),
          "PX4 reset silently rebased the map");
  Require(alignment.Faulted() && !alignment.Transform(),
          "Reset did not invalidate the frame authority");
  Require(!alignment.Observe(Pose(300, 0), Pose(300, 0), 1),
          "Fault recovered without explicit reset");
  alignment.Reset();
  Require(alignment.Observe(Pose(400, 0), Pose(400, 0), 1),
          "Explicit new session could not initialize");
}

void Drift() {
  FrameAlignment alignment(0.2, 0.1);
  Require(alignment.Observe(Pose(100, 0), Pose(100, 0), 0), "Init failed");
  Require(!alignment.Observe(Pose(200, 1), Pose(200, 0), 0),
          "Large localization disagreement was hidden");
  Require(!alignment.Transform(), "Faulted alignment still exposed");
}

void Rotation() {
  FrameAlignment alignment(0.2, 0.1);
  Require(alignment.Observe(Pose(100, 0), Pose(100, 0), 0), "Init failed");
  Require(!alignment.Observe(Pose(200, 0, 0.3), Pose(200, 0), 0),
          "Large heading disagreement accepted");
}
}  // namespace

int main(const int argc, char** argv) {
  try {
    Require(argc == 2, "Specify one test case");
    const std::string name(argv[1]);
    if (name == "interpolation") {
      Interpolation();
    } else if (name == "bounds") {
      Bounds();
    } else if (name == "invalid") {
      Invalid();
    } else if (name == "frame-conversion") {
      Conversion();
    } else if (name == "freshness") {
      Freshness();
    } else if (name == "alignment") {
      Alignment();
    } else if (name == "reset") {
      Reset();
    } else if (name == "drift") {
      Drift();
    } else if (name == "rotation") {
      Rotation();
    } else {
      throw std::runtime_error("Unknown test case");
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
