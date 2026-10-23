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

RDEPENDS:${PN} += "\
    cv-bridge \
    rclcpp \
"
