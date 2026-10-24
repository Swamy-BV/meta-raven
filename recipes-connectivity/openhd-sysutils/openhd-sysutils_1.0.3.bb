SUMMARY = "OpenHD platform configuration and radio discovery daemon"
DESCRIPTION = "SysUtils with host partitioning, update and service management disabled for distribution ownership"
HOMEPAGE = "https://github.com/OpenHD/OpenHD-SysUtils"
BUGTRACKER = "https://github.com/OpenHD/OpenHD-SysUtils/issues"
SECTION = "console/network"
LICENSE = "GPL-3.0-only & OpenHD-NonMilitary"
LIC_FILES_CHKSUM = "\
    file://inc/sysutil_config.h;beginline=1;endline=21;md5=cc659a5f9c4e00330d0f09b939df1415 \
"
SRC_URI = "\
    git://github.com/OpenHD/OpenHD-SysUtils.git;protocol=https;branch=main \
    file://0001-support-distribution-managed-systems.patch \
    file://openhd-sysutils.service \
    file://sysutils.json \
    file://openhd.conf \
"
SRCREV = "4928d7bf3c46197cda92cca72c7b6327204657f5"

inherit cmake systemd

export HOST_CXX = "${BUILD_CXX} ${BUILD_CXXFLAGS} ${BUILD_LDFLAGS}"
EXTRA_OECMAKE = "-DOPENHD_MANAGED_SYSTEM=OFF -DOPENHD_X20_PACKAGE=OFF"
SYSTEMD_SERVICE:${PN} = "openhd-sysutils.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
FILES:${PN}:append = " ${systemd_system_unitdir} ${nonarch_libdir}/tmpfiles.d ${datadir}/openhd"
RDEPENDS:${PN} = "iproute2 iw"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/openhd-sysutils.service \
        ${D}${systemd_system_unitdir}/openhd-sysutils.service
    install -D -m 0644 ${UNPACKDIR}/sysutils.json ${D}${datadir}/openhd/sysutils.json
    install -D -m 0644 ${UNPACKDIR}/openhd.conf ${D}${nonarch_libdir}/tmpfiles.d/openhd.conf
    sed -i 's|@BINDIR@|${bindir}|g; s|@LIBDIR@|${nonarch_libdir}|g; s|@DATADIR@|${datadir}|g' \
        ${D}${systemd_system_unitdir}/openhd-sysutils.service \
        ${D}${nonarch_libdir}/tmpfiles.d/openhd.conf
}
