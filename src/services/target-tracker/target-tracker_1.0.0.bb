SUMMARY = "Bounded ROS object tracking with ByteTrack"
DESCRIPTION = "Class-aware 2D tracking with acquisition-time prediction and standard vision messages"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    bytetrack \
    diagnostic-msgs \
    opencv \
    rcl-interfaces \
    rclcpp \
    sensor-msgs \
    std-msgs \
    std-srvs \
    vision-msgs \
"
SRC_URI = "\
    file://target-tracker.service \
    file://target-tracker.yaml \
"
ROS_BPN = "target_tracker"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd

EXTRA_OECMAKE += "-DBUILD_TESTING=OFF"

SYSTEMD_SERVICE:${PN} = "target-tracker.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
CONFFILES:${PN} = "${sysconfdir}/target-tracker.yaml"
RDEPENDS:${PN} = "\
    diagnostic-msgs \
    raven-ros-config \
    rclcpp \
    rmw-cyclonedds-cpp \
    sensor-msgs \
    std-msgs \
    std-srvs \
    vision-msgs \
"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/target-tracker.yaml ${D}${sysconfdir}/target-tracker.yaml
    install -D -m 0644 ${UNPACKDIR}/target-tracker.service ${D}${systemd_system_unitdir}/target-tracker.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/target-tracker.service
}
inherit raven_source raven_quality
