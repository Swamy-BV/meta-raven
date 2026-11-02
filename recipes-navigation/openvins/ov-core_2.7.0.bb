SUMMARY = "OpenVINS visual-inertial tracking and estimation primitives"
DESCRIPTION = "Upstream camera tracking, IMU propagation and mathematical primitives for ROS 2 VIO estimation"
DEPENDS = "\
    ament-cmake-native \
    boost \
    cv-bridge \
    libeigen \
    opencv \
    rclcpp \
"
require openvins.inc

SRC_URI += "\
    file://0011-add-monocular-opencl-klt.patch;patchdir=.. \
    file://0015-handle-zero-flow-klt-geometry.patch;patchdir=.. \
"

RDEPENDS:${PN} += "\
    cv-bridge \
    rclcpp \
"
