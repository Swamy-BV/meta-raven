SUMMARY = "Raven shared storage and writable state"
DESCRIPTION = "Mounts shared data and persistent partitions and overlays var-lib across A/B slots"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "base"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
SRC_URI = "\
    file://data.mount \
    file://overlay.mount \
    file://persistent.mount \
    file://raven-overlay-prepare.service \
    file://var-lib.conf \
"
S = "${UNPACKDIR}"

inherit overlayfs

OVERLAYFS_WRITABLE_PATHS[state] = "${localstatedir}/lib"
PACKAGE_ARCH = "${MACHINE_ARCH}"
SYSTEMD_SERVICE:${PN} = "data.mount overlay.mount persistent.mount"
FILES:${PN} += "\
    /data \
    /overlay \
    /persistent \
    ${systemd_system_unitdir}/raven-overlay-prepare.service \
    ${systemd_system_unitdir}/var-lib.mount.d \
"
RDEPENDS:${PN} = "kernel-module-overlay"
COMPATIBLE_MACHINE = "raven-frdm-imx95"

do_install() {
    install -D -m 0644 ${UNPACKDIR}/data.mount ${D}${systemd_system_unitdir}/data.mount
    install -D -m 0644 ${UNPACKDIR}/overlay.mount ${D}${systemd_system_unitdir}/overlay.mount
    install -D -m 0644 ${UNPACKDIR}/persistent.mount ${D}${systemd_system_unitdir}/persistent.mount
    install -D -m 0644 ${UNPACKDIR}/raven-overlay-prepare.service \
        ${D}${systemd_system_unitdir}/raven-overlay-prepare.service
    install -D -m 0644 ${UNPACKDIR}/var-lib.conf \
        ${D}${systemd_system_unitdir}/var-lib.mount.d/10-raven.conf
    install -d ${D}/data ${D}/overlay ${D}/persistent ${D}${localstatedir}/lib
}
