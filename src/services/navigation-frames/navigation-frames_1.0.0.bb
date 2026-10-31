SUMMARY = "Acquisition-time VIO and PX4 frame authority"
DESCRIPTION = "Align local navigation frames with timestamp brackets, estimator reset rejection and fusion health"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "robotics"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    diagnostic-msgs \
    geometry-msgs \
    libeigen \
    nav-msgs \
    px4-msgs \
    rcl-interfaces \
    rclcpp \
    std-srvs \
    tf2-ros \
"
SRC_URI = "file://navigation-frames.service file://navigation-frames.yaml"
ROS_BPN = "navigation_frames"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd

EXTRA_OECMAKE += "-DBUILD_TESTING=OFF"
SYSTEMD_SERVICE:${PN} = "navigation-frames.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/navigation-frames.yaml"
RDEPENDS:${PN} = "\
    diagnostic-msgs \
    geometry-msgs \
    nav-msgs \
    px4-msgs \
    raven-ros-config \
    rclcpp \
    rmw-cyclonedds-cpp \
    std-srvs \
    tf2-ros \
"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/navigation-frames.yaml ${D}${sysconfdir}/navigation-frames.yaml
    install -D -m 0644 ${UNPACKDIR}/navigation-frames.service ${D}${systemd_system_unitdir}/navigation-frames.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/navigation-frames.service
}
inherit raven_source raven_quality
