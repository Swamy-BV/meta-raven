SUMMARY = "Optional OpenHD encoded video and telemetry radio backend"
DESCRIPTION = "Headless OpenHD using external RTP input and the existing flight-controller MAVLink router"
HOMEPAGE = "https://github.com/OpenHD/OpenHD"
BUGTRACKER = "https://github.com/OpenHD/OpenHD/issues"
SECTION = "console/network"
LICENSE = "GPL-3.0-only & MIT & OpenHD-NonMilitary"
LIC_FILES_CHKSUM = "\
    file://LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464 \
    file://OpenHD/ohd_common/lib/json/LICENSE.MIT;md5=e046aa7fd39db177f5ca0cefe06ddbdc \
    file://OpenHD/ohd_common/lib/spdlog/LICENSE;md5=13886a8d1692948ea9b78edc50e9447c \
    file://OpenHD/ohd_common/inc/config_paths.h;beginline=11;endline=15;md5=6cde1aec9aaf3307eafe1973b6489082 \
"
DEPENDS = "gstreamer1.0 gstreamer1.0-plugins-base libpcap libsodium poco python3-native"
SRC_URI = "\
    gitsm://github.com/OpenHD/OpenHD.git;protocol=https;branch=2.7-evo \
    file://0001-use-persistent-state-and-router-endpoint.patch \
    file://0002-include-required-standard-headers.patch \
    file://openhd@.service \
    file://mavlink-router.conf \
    file://media-capture.yaml \
"
SRCREV = "c75c37e6bcd014a990b81ae9b2556eb6afefd924"
OECMAKE_SOURCEPATH = "${S}/OpenHD"

inherit cmake pkgconfig systemd

EXTRA_OECMAKE = "-DENABLE_AIR=ON -DENABLE_USB_CAMERAS=OFF"
OECMAKE_TARGET_COMPILE = "openhd"
SYSTEMD_SERVICE:${PN} = "openhd@wfb0.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
FILES:${PN}:append = " ${systemd_system_unitdir}"
RDEPENDS:${PN} = "\
    gstreamer1.0-plugins-bad-videoparsersbad \
    gstreamer1.0-plugins-base-app \
    gstreamer1.0-plugins-good-rtp \
    gstreamer1.0-plugins-good-udp \
    iproute2 \
    iw \
    mavlink-router \
    openhd-sysutils \
    util-linux-rfkill \
"
CONFFILES:${PN} = "${sysconfdir}/mavlink-router/config.d/openhd.conf"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/openhd@.service ${D}${systemd_system_unitdir}/openhd@.service
    install -D -m 0644 ${UNPACKDIR}/media-capture.yaml ${D}${datadir}/openhd/media-capture.yaml
    install -D -m 0644 ${UNPACKDIR}/mavlink-router.conf ${D}${sysconfdir}/mavlink-router/config.d/openhd.conf
    sed -i 's|@BINDIR@|${bindir}|g; s|@DATADIR@|${datadir}|g' \
        ${D}${systemd_system_unitdir}/openhd@.service
}
