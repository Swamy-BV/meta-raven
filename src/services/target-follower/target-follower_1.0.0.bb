SUMMARY = "Explicit selected-target visual following"
DESCRIPTION = "ROS target selection and bounded PX4 offboard requests with target-loss hold"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "robotics"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    diagnostic-msgs \
    platform-interfaces \
    px4-msgs \
    rcl-interfaces \
    rclcpp \
    sensor-msgs \
    std-srvs \
    vision-msgs \
"
SRC_URI = "file://target-follower.service file://target-follower.yaml"
ROS_BPN = "target_follower"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd

SYSTEMD_SERVICE:${PN} = "target-follower.service"
# Following is activated explicitly; installation must not start flight behavior.
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/target-follower.yaml"
RDEPENDS:${PN} = "\
    diagnostic-msgs \
    platform-interfaces \
    px4-msgs \
    raven-ros-config \
    rclcpp \
    rmw-cyclonedds-cpp \
    sensor-msgs \
    std-srvs \
    vision-msgs \
"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/target-follower.yaml ${D}${sysconfdir}/target-follower.yaml
    install -D -m 0644 ${UNPACKDIR}/target-follower.service ${D}${systemd_system_unitdir}/target-follower.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/target-follower.service
}
inherit raven_source raven_quality
