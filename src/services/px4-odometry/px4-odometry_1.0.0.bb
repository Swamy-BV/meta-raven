SUMMARY = "Validated ROS odometry adapter for PX4 external vision"
DESCRIPTION = "Maps fresh calibrated ROS odometry into PX4 FRD measurements over the existing DDS link"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "robotics"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    diagnostic-msgs \
    geometry-msgs \
    libeigen \
    nav-msgs \
    px4-msgs \
    rclcpp \
"
SRC_URI = "file://px4-odometry.service file://px4-odometry.yaml"
ROS_BPN = "px4_odometry"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd

SYSTEMD_SERVICE:${PN} = "px4-odometry.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/px4-odometry.yaml"
RDEPENDS:${PN} = "diagnostic-msgs geometry-msgs nav-msgs px4-msgs raven-ros-config rclcpp rmw-cyclonedds-cpp"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/px4-odometry.yaml ${D}${sysconfdir}/px4-odometry.yaml
    install -D -m 0644 ${UNPACKDIR}/px4-odometry.service ${D}${systemd_system_unitdir}/px4-odometry.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/px4-odometry.service
}
inherit raven_source raven_quality
