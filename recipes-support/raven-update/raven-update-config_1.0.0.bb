SUMMARY = "Raven A/B update selection and boot confirmation"
DESCRIPTION = "Selects the inactive SWUpdate slot and confirms successful Raven boots"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "base"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
SRC_URI = "\
    file://20-raven-args \
    file://hwrevision \
    file://raven-update-confirm \
    file://raven-update-confirm.service \
    file://raven.conf \
"
S = "${UNPACKDIR}"

inherit systemd

PACKAGE_ARCH = "${MACHINE_ARCH}"
SYSTEMD_SERVICE:${PN} = "raven-update-confirm.service"
FILES:${PN} += "${systemd_system_unitdir}/swupdate.service.d"
RDEPENDS:${PN} = "\
    libubootenv-bin \
    mavlink-router \
    micro-xrce-dds-agent \
    raven-storage \
    u-boot-imx-env \
    util-linux-findmnt \
"
COMPATIBLE_MACHINE = "raven-frdm-imx95"

do_install() {
    install -D -m 0644 ${UNPACKDIR}/hwrevision ${D}${sysconfdir}/hwrevision
    install -D -m 0644 ${UNPACKDIR}/20-raven-args ${D}${sysconfdir}/swupdate/conf.d/20-raven-args
    install -D -m 0644 ${UNPACKDIR}/raven-update-confirm.service \
        ${D}${systemd_system_unitdir}/raven-update-confirm.service
    install -D -m 0644 ${UNPACKDIR}/raven.conf \
        ${D}${systemd_system_unitdir}/swupdate.service.d/raven.conf
    install -D -m 0755 ${UNPACKDIR}/raven-update-confirm ${D}${libexecdir}/raven-update-confirm
}
