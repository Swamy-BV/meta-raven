SUMMARY = "MAVLink camera commands backed by ROS capture operations"
DESCRIPTION = "Camera protocol component for ground stations and PX4 missions through mavlink-router"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    mavlink \
    platform-interfaces \
    rclcpp \
    rclcpp-action \
"
SRC_URI = "file://camera-mavlink.service file://camera-mavlink.yaml"
ROS_BPN = "camera_mavlink"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd useradd raven_source raven_quality

SYSTEMD_SERVICE:${PN} = "camera-mavlink.service"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 978 camera-mavlink"
USERADD_PARAM:${PN} = "--system --uid 978 --gid camera-mavlink --no-create-home --shell /sbin/nologin camera-mavlink"
CONFFILES:${PN} = "${sysconfdir}/camera-mavlink.yaml"
RDEPENDS:${PN} = "mavlink-router media-capture raven-ros-config rmw-cyclonedds-cpp"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/camera-mavlink.yaml ${D}${sysconfdir}/camera-mavlink.yaml
    install -D -m 0644 ${UNPACKDIR}/camera-mavlink.service ${D}${systemd_system_unitdir}/camera-mavlink.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/camera-mavlink.service
}
