SUMMARY = "Raven ROS 2 perception output adapter"
DESCRIPTION = "Publishes standard vision results, model metadata and pipeline health with explicit result identity"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    diagnostic-msgs \
    glib-2.0 \
    local-ipc \
    nlohmann-json \
    platform-interfaces \
    rcl-interfaces \
    rclcpp \
    std-msgs \
    vision-msgs \
"
ROS_BPN = "perception_hub"
SRC_URI = "file://perception-hub.service"
RDEPENDS:${PN} = "\
    diagnostic-msgs \
    model-runner \
    platform-interfaces \
    rcl-interfaces \
    rclcpp \
    rmw-cyclonedds-cpp \
    std-msgs \
    vision-msgs \
"
inherit ros_distro_jazzy ros_component ros_ament_cmake pkgconfig systemd raven_source raven_quality
SYSTEMD_SERVICE:${PN} = "perception-hub.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/perception-hub.service ${D}${systemd_system_unitdir}/perception-hub.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/perception-hub.service
}
