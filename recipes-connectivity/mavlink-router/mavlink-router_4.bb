SUMMARY = "Route PX4 MAVLink between RPMsg and ground stations"
DESCRIPTION = "Upstream MAVLink router with independent PX4 and ground-station endpoints alongside ROS 2 DDS"
HOMEPAGE = "https://github.com/mavlink-router/mavlink-router"
BUGTRACKER = "https://github.com/mavlink-router/mavlink-router/issues"
SECTION = "console/network"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=93888867ace35ffec2c845ea90b2e16b"
DEPENDS = "systemd"
# Follow the NXP Robotics baseline, including its GCC 15 header fix.
# nooelint: oelint.file.patchsignedoff Preserve the vendor patch attribution; its original has no sign-off.
SRC_URI = "\
    gitsm://github.com/mavlink-router/mavlink-router.git;protocol=https;branch=master \
    file://0001-dedup-include-cstdint.patch \
    file://0002-disable-nagle-for-tcp-clients.patch \
    file://0003-retain-partial-uart-writes.patch \
    file://main.conf \
    file://mavlink-router.service \
    file://99-px4-mavlink.rules \
"
SRCREV = "c20337bbc61abda21463e7482172d58b654e66b6"

inherit meson pkgconfig systemd useradd

EXTRA_OEMESON = "-Dsystemdsystemunitdir=${systemd_system_unitdir}"
SYSTEMD_SERVICE:${PN} = "mavlink-router.service"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 976 mavlink-router"
USERADD_PARAM:${PN} = "--system --uid 976 --gid mavlink-router --home-dir / \
                       --no-create-home --shell /sbin/nologin mavlink-router"
CONFFILES:${PN} = "${sysconfdir}/mavlink-router/main.conf"
FILES:${PN}:append = " ${nonarch_base_libdir}/udev/rules.d/99-px4-mavlink.rules"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/main.conf ${D}${sysconfdir}/mavlink-router/main.conf
    install -D -m 0644 ${UNPACKDIR}/mavlink-router.service ${D}${systemd_system_unitdir}/mavlink-router.service
    install -D -m 0644 ${UNPACKDIR}/99-px4-mavlink.rules \
        ${D}${nonarch_base_libdir}/udev/rules.d/99-px4-mavlink.rules
    sed -i 's|@BINDIR@|${bindir}|g' ${D}${systemd_system_unitdir}/mavlink-router.service
}
