// SPDX-License-Identifier: AGPL-3.0-only
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "core/alignment.h"
#include "node.h"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"

namespace raven::navigation {
namespace {
constexpr double kQuaternionTolerance = 1e-3;
}

void FramesNode::Configure() {
  rcl_interfaces::msg::ParameterDescriptor options;
  options.read_only = true;
  const auto text = [this, &options](const std::string& name,
                                     const std::string& value) {
    const auto result = declare_parameter<std::string>(name, value, options);
    if (result.empty()) {
      throw std::invalid_argument("Empty parameter: " + name);
    }
    return result;
  };
  input_frame_ = text("vio_world_frame", "global");
  imu_frame_ = text("vio_imu_frame", "imu");
  local_frame_ = text("local_frame", "vio_local");
  reference_frame_ = text("reference_frame", "px4_local_enu");
  body_frame_ = text("body_frame", "base_link");
  if (std::set<std::string>{local_frame_, reference_frame_, body_frame_}
          .size() != 3) {
    throw std::invalid_argument("TF parent and child frames must be distinct");
  }
  local_topic_ = text("vio_topic", "/localization/vio/odometry");
  px4_namespace_ =
      declare_parameter<std::string>("px4_namespace", "/simulation", options);
  reference_topic_ =
      text("reference_topic", px4_namespace_ + "/fmu/out/vehicle_odometry");
  max_age_ns_ =
      declare_parameter<std::int64_t>("max_age_ns", max_age_ns_, options);
  if (max_age_ns_ <= 0 || max_age_ns_ > kNanosecondsPerSecond) {
    throw std::invalid_argument("Acquisition age must be in (0, 1 second]");
  }
  const auto xyz = declare_parameter<std::vector<double>>("imu_from_body_xyz",
                                                          {0, 0, 0}, options);
  const auto wxyz = declare_parameter<std::vector<double>>(
      "imu_from_body_wxyz", {1, 0, 0, 0}, options);
  if (xyz.size() != 3 || wxyz.size() != 4) {
    throw std::invalid_argument("Mount transform requires XYZ and WXYZ");
  }
  const Eigen::Quaterniond rotation(wxyz[0], wxyz[1], wxyz[2], wxyz[3]);
  if (!rotation.coeffs().allFinite() ||
      std::abs(rotation.norm() - 1) > kQuaternionTolerance) {
    throw std::invalid_argument(
        "Mount quaternion must be finite and unit length");
  }
  imu_from_body_.translation() = Eigen::Vector3d(xyz[0], xyz[1], xyz[2]);
  imu_from_body_.linear() = rotation.normalized().toRotationMatrix();
  if (!ValidTransform(imu_from_body_)) {
    throw std::invalid_argument("Invalid IMU-to-body transform");
  }
}

}  // namespace raven::navigation
