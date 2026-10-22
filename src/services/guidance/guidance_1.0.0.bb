SUMMARY = "Modular PX4 motion guidance"
DESCRIPTION = "ROS GoTo, selected-target following and orbit with bounded motion, exclusive ownership and loss hold"
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
    platform-interfaces \
    px4-msgs \
    rcl-interfaces \
    rclcpp \
    rclcpp-action \
    ruckig \
    sensor-msgs \
    std-srvs \
    vision-msgs \
"
SRC_URI = "file://guidance.service file://guidance.yaml"
ROS_BPN = "guidance"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd

SYSTEMD_SERVICE:${PN} = "guidance.service"
# Following is activated explicitly; installation must not start flight behavior.
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/guidance.yaml"
RDEPENDS:${PN} = "\
    diagnostic-msgs \
    geometry-msgs \
    platform-interfaces \
    px4-msgs \
    raven-ros-config \
    rclcpp \
    rclcpp-action \
    rmw-cyclonedds-cpp \
    ruckig \
    sensor-msgs \
    std-srvs \
    vision-msgs \
"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/guidance.yaml ${D}${sysconfdir}/guidance.yaml
    install -D -m 0644 ${UNPACKDIR}/guidance.service ${D}${systemd_system_unitdir}/guidance.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/guidance.service
}
inherit raven_source raven_quality
