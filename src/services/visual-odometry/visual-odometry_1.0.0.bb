SUMMARY = "Visual-inertial odometry deployment for GPS-denied evaluation"
DESCRIPTION = "Explicitly activated OpenVINS ROS 2 service for calibrated camera-IMU odometry evaluation"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "navigation"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven deployment files use the common license text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"

SRC_URI = "file://visual-odometry.service file://visual-odometry.yaml"
S = "${UNPACKDIR}"
inherit allarch ros_distro_jazzy ros_component systemd

SYSTEMD_SERVICE:${PN} = "visual-odometry.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/visual-odometry/ros.yaml"
RDEPENDS:${PN} = "ov-msckf raven-ros-config rmw-cyclonedds-cpp"

do_install() {
    install -D -m 0644 ${UNPACKDIR}/visual-odometry.yaml ${D}${sysconfdir}/visual-odometry/ros.yaml
    install -D -m 0644 ${UNPACKDIR}/visual-odometry.service ${D}${systemd_system_unitdir}/visual-odometry.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/visual-odometry.service
}
