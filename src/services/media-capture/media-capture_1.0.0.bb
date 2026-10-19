SUMMARY = "Video capture, photos, recording and RTP streaming"
DESCRIPTION = "ROS 2 camera node with hardware-aware capture, photos, recording and standard image publication"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    diagnostic-msgs \
    gdk-pixbuf \
    glib-2.0 \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    image-transport \
    platform-contracts \
    platform-interfaces \
    rcl-interfaces \
    rclcpp \
    rclcpp-action \
    sensor-msgs \
"
SRC_URI = "file://media-capture.yaml file://media-capture.service file://media-capture.conf"
SRC_URI:append:raven-frdm-imx95 = " file://media-capture-wave6.conf"

ROS_BPN = "media_capture"
inherit ros_distro_jazzy ros_component ros_ament_cmake pkgconfig systemd useradd

SYSTEMD_SERVICE:${PN} = "media-capture.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
SYSTEMD_AUTO_ENABLE:${PN}:raven-frdm-imx95 = "enable"
USERADD_PACKAGES = "${PN}"
# Reserve UID/GID 970 across A/B slots; future service identities use separate IDs.
GROUPADD_PARAM:${PN} = "--system --gid 970 media-capture"
USERADD_PARAM:${PN} = "--system --uid 970 --gid media-capture --home-dir ${localstatedir}/lib/media-capture \
                       --no-create-home --shell /sbin/nologin media-capture"
CONFFILES:${PN} = "${sysconfdir}/media-capture.yaml"
FILES:${PN}:append = " ${systemd_system_unitdir}/media-capture.service.d ${nonarch_libdir}/tmpfiles.d"
RDEPENDS:${PN} = "\
    gstreamer1.0-plugins-bad-videoparsersbad \
    gstreamer1.0-plugins-base-app \
    gstreamer1.0-plugins-base-playback \
    gstreamer1.0-plugins-base-typefindfunctions \
    gstreamer1.0-plugins-base-videoconvertscale \
    gstreamer1.0-plugins-base-videorate \
    gstreamer1.0-plugins-base-videotestsrc \
    gstreamer1.0-plugins-good-imagefreeze \
    gstreamer1.0-plugins-good-isomp4 \
    gstreamer1.0-plugins-good-jpeg \
    gstreamer1.0-plugins-good-matroska \
    gstreamer1.0-plugins-good-multifile \
    gstreamer1.0-plugins-good-rtp \
    gstreamer1.0-plugins-good-rtpmanager \
    gstreamer1.0-plugins-good-udp \
    gstreamer1.0-plugins-good-video4linux2 \
    gstreamer1.0-plugins-good-vpx \
    image-transport \
    platform-interfaces \
    rclcpp \
    rmw-cyclonedds-cpp \
    sensor-msgs \
"
RDEPENDS:${PN}:append:raven-frdm-imx95 = " imx-gst1.0-plugin-video libcamera-gst"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/media-capture.conf ${D}${nonarch_libdir}/tmpfiles.d/media-capture.conf
    install -D -m 0644 ${UNPACKDIR}/media-capture.yaml ${D}${sysconfdir}/media-capture.yaml
    install -D -m 0644 ${UNPACKDIR}/media-capture.service ${D}${systemd_system_unitdir}/media-capture.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/media-capture.service
}

do_install:append:raven-frdm-imx95() {
    install -D -m 0644 ${UNPACKDIR}/media-capture-wave6.conf \
        ${D}${systemd_system_unitdir}/media-capture.service.d/10-wave6.conf
}
inherit raven_source raven_quality
