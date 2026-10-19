SUMMARY = "Raven A/B updates and persistent state"
DESCRIPTION = "Installs A/B update selection, persistent networking and shared writable state on a read-only root"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "base"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
SRC_URI = "file://hwrevision file://persistent.mount file://data.mount \
           file://80-raven-wifi.network file://raven-wifi.conf file://raven-update-confirm \
           file://raven-update-confirm.service file://20-raven-args \
           file://raven.conf file://overlay.mount \
           file://raven-overlay-prepare.service file://var-lib.conf"
S = "${UNPACKDIR}"

inherit overlayfs
OVERLAYFS_WRITABLE_PATHS[state] = "${localstatedir}/lib"
PACKAGE_ARCH = "${MACHINE_ARCH}"
SYSTEMD_SERVICE:${PN} = "data.mount overlay.mount persistent.mount raven-update-confirm.service"
FILES:${PN} += "${systemd_system_unitdir} /data /overlay /persistent"
RDEPENDS:${PN} = "kernel-module-overlay libubootenv-bin mavlink-router micro-xrce-dds-agent \
                  u-boot-imx-env util-linux-findmnt wpa-supplicant"
COMPATIBLE_MACHINE = "raven-frdm-imx95"

do_install() {
    install -d ${D}${sysconfdir}/swupdate/conf.d
    install -m 0644 ${UNPACKDIR}/hwrevision ${D}${sysconfdir}/hwrevision
    install -m 0644 ${UNPACKDIR}/20-raven-args ${D}${sysconfdir}/swupdate/conf.d/
    install -d ${D}${systemd_system_unitdir}/wpa_supplicant@mlan0.service.d
    install -m 0644 ${UNPACKDIR}/raven-wifi.conf ${D}${systemd_system_unitdir}/wpa_supplicant@mlan0.service.d/
    install -d ${D}${systemd_system_unitdir}/multi-user.target.wants
    ln -s ../wpa_supplicant@.service ${D}${systemd_system_unitdir}/multi-user.target.wants/wpa_supplicant@mlan0.service
    install -m 0644 ${UNPACKDIR}/persistent.mount ${D}${systemd_system_unitdir}/
    install -m 0644 ${UNPACKDIR}/data.mount ${D}${systemd_system_unitdir}/
    install -m 0644 ${UNPACKDIR}/overlay.mount ${D}${systemd_system_unitdir}/
    install -m 0644 ${UNPACKDIR}/raven-overlay-prepare.service ${D}${systemd_system_unitdir}/
    install -d ${D}/data ${D}/overlay ${D}/persistent ${D}${localstatedir}/lib
    install -d -m 0700 ${D}${localstatedir}/lib/wpa_supplicant
    install -d ${D}${systemd_system_unitdir}/var-lib.mount.d
    install -m 0644 ${UNPACKDIR}/var-lib.conf ${D}${systemd_system_unitdir}/var-lib.mount.d/10-raven.conf
    install -m 0644 ${UNPACKDIR}/raven-update-confirm.service ${D}${systemd_system_unitdir}/
    install -d ${D}${systemd_system_unitdir}/swupdate.service.d
    install -m 0644 ${UNPACKDIR}/raven.conf ${D}${systemd_system_unitdir}/swupdate.service.d/
    install -d ${D}${sysconfdir}/systemd/network ${D}${libexecdir}
    install -m 0644 ${UNPACKDIR}/80-raven-wifi.network ${D}${sysconfdir}/systemd/network/
    install -m 0755 ${UNPACKDIR}/raven-update-confirm ${D}${libexecdir}/
}
