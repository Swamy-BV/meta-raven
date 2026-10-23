SUMMARY = "OpenVINS ROS 2 visual-inertial odometry estimator"
DESCRIPTION = "Upstream MSCKF estimator consuming camera and IMU measurements and publishing ROS odometry"
DEPENDS = "\
    ament-cmake-native \
    boost \
    ceres-solver \
    cv-bridge \
    geometry-msgs \
    image-transport \
    libeigen \
    nav-msgs \
    opencv \
    ov-core \
    ov-init \
    rclcpp \
    sensor-msgs \
    std-msgs \
    tf2-geometry-msgs \
    tf2-ros \
"
require openvins.inc

SRC_URI += "\
    file://0002-use-sensor-data-image-qos.patch;patchdir=.. \
    file://0003-use-jazzy-image-headers.patch;patchdir=.. \
    file://0004-bound-ros-executor-and-imu-history.patch;patchdir=.. \
    file://0005-own-asynchronous-imu-timestamp.patch;patchdir=.. \
    file://0006-separate-inertial-callback-group.patch;patchdir=.. \
"

# Keep the feature-level simulator out of the normal runtime installation.
PACKAGES =+ "${PN}-simulation"
FILES:${PN}-simulation += "${ros_libdir}/${ROS_BPN}/run_simulation"

RDEPENDS:${PN} += "\
    cv-bridge \
    geometry-msgs \
    image-transport \
    nav-msgs \
    ov-core \
    ov-init \
    rclcpp \
    sensor-msgs \
    std-msgs \
    tf2-geometry-msgs \
    tf2-ros \
"
