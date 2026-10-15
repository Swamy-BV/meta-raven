SUMMARY = "Raven read-only ROS 2 flight gateway"
DESCRIPTION = "Publishes typed connection, armed state, mode and diagnostic health using a replaceable MAVSDK backend"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "console/utils"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "ament-cmake-native diagnostic-msgs mavsdk platform-interfaces rclcpp"
ROS_BPN = "flight_link"
SRC_URI = "file://flight-link.service file://99-flight-link.rules"
FILES:${PN}:append = " ${nonarch_base_libdir}/udev/rules.d/99-flight-link.rules"
RDEPENDS:${PN} = "diagnostic-msgs platform-interfaces rclcpp rmw-cyclonedds-cpp"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd useradd raven_source raven_quality
SYSTEMD_SERVICE:${PN} = "flight-link.service"
# Enable only after ensuring no other process owns the configured serial endpoint.
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 974 flight-link"
USERADD_PARAM:${PN} = "--system --uid 974 --gid flight-link --home-dir ${localstatedir}/lib/flight-link \
                       --no-create-home --shell /sbin/nologin flight-link"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/flight-link.service ${D}${systemd_system_unitdir}/flight-link.service
    install -D -m 0644 ${UNPACKDIR}/99-flight-link.rules ${D}${nonarch_base_libdir}/udev/rules.d/99-flight-link.rules
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/flight-link.service
}
