SUMMARY = "OpenVINS visual-inertial initialization"
DESCRIPTION = "Upstream static and dynamic camera-IMU initialization library for visual-inertial odometry"
DEPENDS = "\
    ament-cmake-native \
    boost \
    ceres-solver \
    cv-bridge \
    libeigen \
    opencv \
    ov-core \
    rclcpp \
"
require openvins.inc

RDEPENDS:${PN} += "\
    cv-bridge \
    ov-core \
    rclcpp \
"
