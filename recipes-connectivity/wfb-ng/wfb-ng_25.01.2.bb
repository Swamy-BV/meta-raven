SUMMARY = "Encrypted video and MAVLink transport over raw Wi-Fi"
DESCRIPTION = "WFB-ng native radio data plane and upstream Python supervisor with opt-in airborne configuration"
HOMEPAGE = "https://github.com/svpcom/wfb-ng"
BUGTRACKER = "https://github.com/svpcom/wfb-ng/issues"
SECTION = "console/network"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=84dcc94da3adb52b53ae4fa38fe49e5d"
DEPENDS = "libevent libpcap libsodium"
SRC_URI = "\
    git://github.com/svpcom/wfb-ng.git;protocol=https;branch=stable \
    file://air.cfg \
    file://ground.cfg \
    file://wfb-ng@.service \
    file://wfb-ng.conf \
    file://mavlink-router.conf \
    file://media-capture.yaml \
"
SRCREV = "ec936d5153461b4b1c57ca90f732373170ecb37c"

inherit setuptools3 pkgconfig systemd

# Upstream setup.py can omit distribution-specific installers. Install the
# native binaries separately so ELF files are not hidden inside a Python wheel.
export VERSION = "${PV}"
export COMMIT = "${SRCREV}"
export OMIT_DATA_FILES = "1"
EXTRA_OEMAKE = "VERSION=${PV} COMMIT=${SRCREV} RELEASE=release-${PV}"

do_compile:prepend() {
    oe_runmake all_bin
}

SYSTEMD_SERVICE:${PN} = "wfb-ng@wfb0.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/wfb-ng/air.cfg ${sysconfdir}/mavlink-router/config.d/wfb-ng.conf"
FILES:${PN}:append = " ${systemd_system_unitdir} ${nonarch_libdir}/tmpfiles.d"
RDEPENDS:${PN} = "\
    iproute2 \
    iw \
    mavlink-router \
    python3-asyncio \
    python3-compression \
    python3-core \
    python3-crypt \
    python3-curses \
    python3-io \
    python3-jinja2 \
    python3-json \
    python3-logging \
    python3-misc \
    python3-msgpack \
    python3-netclient \
    python3-pyroute2 \
    python3-pyserial \
    python3-pyyaml \
    python3-shell \
    python3-sqlite3 \
    python3-twisted-conch \
    python3-twisted-core \
    python3-twisted-protocols \
"

do_install:append() {
    for binary in wfb_tx wfb_rx wfb_keygen wfb_tx_cmd wfb_tun; do
        install -D -m 0755 ${S}/$binary ${D}${bindir}/$binary
    done
    install -D -m 0644 ${UNPACKDIR}/wfb-ng@.service ${D}${systemd_system_unitdir}/wfb-ng@.service
    install -D -m 0644 ${UNPACKDIR}/air.cfg ${D}${sysconfdir}/wfb-ng/air.cfg
    install -D -m 0644 ${UNPACKDIR}/ground.cfg ${D}${datadir}/wfb-ng/ground.cfg
    install -D -m 0644 ${UNPACKDIR}/media-capture.yaml ${D}${datadir}/wfb-ng/media-capture.yaml
    install -D -m 0644 ${UNPACKDIR}/wfb-ng.conf ${D}${nonarch_libdir}/tmpfiles.d/wfb-ng.conf
    install -D -m 0644 ${UNPACKDIR}/mavlink-router.conf ${D}${sysconfdir}/mavlink-router/config.d/wfb-ng.conf
    sed -i 's|@BINDIR@|${bindir}|g; s|@SYSCONFDIR@|${sysconfdir}|g; s|@LOCALSTATEDIR@|${localstatedir}|g' \
        ${D}${systemd_system_unitdir}/wfb-ng@.service \
        ${D}${sysconfdir}/wfb-ng/air.cfg \
        ${D}${datadir}/wfb-ng/ground.cfg \
        ${D}${nonarch_libdir}/tmpfiles.d/wfb-ng.conf
}
