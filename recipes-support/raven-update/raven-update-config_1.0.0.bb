SUMMARY = "Raven A/B update and persistent network configuration"
DESCRIPTION = "Installs A/B update selection, boot confirmation and persistent Wi-Fi configuration"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "base"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
SRC_URI = "file://fw_env.config file://hwrevision file://persistent.mount file://data.mount \
           file://80-raven-wifi.network file://raven-wifi.conf file://raven-update-confirm \
           file://raven-update-confirm.service file://20-raven-args \
           file://raven.conf"
S = "${UNPACKDIR}"

inherit systemd
PACKAGE_ARCH = "${MACHINE_ARCH}"
do_install[depends] += "u-boot-imx:do_deploy"
SYSTEMD_SERVICE:${PN} = "data.mount persistent.mount raven-update-confirm.service"
FILES:${PN} += "${systemd_system_unitdir}"
RDEPENDS:${PN} = "libubootenv-bin util-linux-findmnt wpa-supplicant"
COMPATIBLE_MACHINE = "raven-frdm-imx95"

do_install() {
    install -d ${D}${sysconfdir}/swupdate/conf.d ${D}${sysconfdir}/wpa_supplicant
    install -m 0644 ${DEPLOY_DIR_IMAGE}/u-boot-imx-initial-env-${MACHINE}-${UBOOT_CONFIG} \
        ${D}${sysconfdir}/u-boot-initial-env
    install -m 0644 ${UNPACKDIR}/fw_env.config ${D}${sysconfdir}/fw_env.config
    install -m 0644 ${UNPACKDIR}/hwrevision ${D}${sysconfdir}/hwrevision
    install -m 0644 ${UNPACKDIR}/20-raven-args ${D}${sysconfdir}/swupdate/conf.d/
    ln -s /persistent/wifi/wpa_supplicant-mlan0.conf ${D}${sysconfdir}/wpa_supplicant/wpa_supplicant-mlan0.conf
    install -d ${D}${systemd_system_unitdir}/wpa_supplicant@mlan0.service.d
    install -m 0644 ${UNPACKDIR}/raven-wifi.conf ${D}${systemd_system_unitdir}/wpa_supplicant@mlan0.service.d/
    install -d ${D}${systemd_system_unitdir}/multi-user.target.wants
    ln -s ../wpa_supplicant@.service ${D}${systemd_system_unitdir}/multi-user.target.wants/wpa_supplicant@mlan0.service
    install -m 0644 ${UNPACKDIR}/persistent.mount ${D}${systemd_system_unitdir}/
    install -m 0644 ${UNPACKDIR}/data.mount ${D}${systemd_system_unitdir}/
    install -m 0644 ${UNPACKDIR}/raven-update-confirm.service ${D}${systemd_system_unitdir}/
    install -d ${D}${systemd_system_unitdir}/swupdate.service.d
    install -m 0644 ${UNPACKDIR}/raven.conf ${D}${systemd_system_unitdir}/swupdate.service.d/
    install -d ${D}${sysconfdir}/systemd/network ${D}${libexecdir}
    install -m 0644 ${UNPACKDIR}/80-raven-wifi.network ${D}${sysconfdir}/systemd/network/
    install -m 0755 ${UNPACKDIR}/raven-update-confirm ${D}${libexecdir}/
}
