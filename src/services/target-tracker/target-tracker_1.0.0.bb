SUMMARY = "Bounded ROS object tracking with ByteTrack"
DESCRIPTION = "Class-aware 2D tracking with acquisition-time prediction and standard vision messages"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only & MIT"
# nooelint: oelint.var.licenseremotefile Raven adapter uses the common license; upstream retains MIT.
LIC_FILES_CHKSUM = "\
    file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788 \
    file://${UNPACKDIR}/bytetrack/LICENSE;md5=e3d0d8afca6872e02b102c0776327a07 \
"
DEPENDS = "\
    ament-cmake-native \
    diagnostic-msgs \
    libeigen \
    rcl-interfaces \
    rclcpp \
    std-msgs \
    std-srvs \
    vision-msgs \
"
SRC_URI = "\
    git://github.com/FoundationVision/ByteTrack.git;protocol=https;branch=main;destsuffix=bytetrack \
    file://0001-bound-tracking-and-use-acquisition-time.patch;patchdir=${UNPACKDIR}/bytetrack \
    file://0002-recover-retained-identities-with-weak-observations.patch;patchdir=${UNPACKDIR}/bytetrack \
    file://target-tracker.service \
    file://target-tracker.yaml \
"
SRCREV = "d1bf0191adff59bc8fcfeaa0b33d3d1642552a99"
BB_GIT_SHALLOW = "1"
ROS_BPN = "target_tracker"
inherit ros_distro_jazzy ros_component ros_ament_cmake systemd

CXXFLAGS:append = " -ffile-prefix-map=${UNPACKDIR}/bytetrack=/usr/src/debug/${PN}/${PV}/bytetrack"
EXTRA_OECMAKE += "-DBYTETRACK_SOURCE_DIR=${UNPACKDIR}/bytetrack/deploy/TensorRT/cpp"
SYSTEMD_SERVICE:${PN} = "target-tracker.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
CONFFILES:${PN} = "${sysconfdir}/target-tracker.yaml"
RDEPENDS:${PN} = "\
    diagnostic-msgs \
    raven-ros-config \
    rclcpp \
    rmw-cyclonedds-cpp \
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
