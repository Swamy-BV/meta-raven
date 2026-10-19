FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"
SRC_URI:append:raven-frdm-imx95 = " file://overlay-storage.conf file://authorized-keys.conf"

# The shared /var/lib overlay retains SSH identity and access across A/B updates.
OPENSSH_HOST_KEY_DIR:raven-frdm-imx95 = "${localstatedir}/lib/ssh"
OPENSSH_HOST_KEY_DIR_READONLY_CONFIG:raven-frdm-imx95 = "${localstatedir}/lib/ssh"

FILES:${PN}-sshd:append:raven-frdm-imx95 = " ${sysconfdir}/ssh/sshd_config.d"
CONFFILES:${PN}-sshd:append:raven-frdm-imx95 = " ${sysconfdir}/ssh/sshd_config.d/10-raven.conf"

do_install:append:raven-frdm-imx95() {
    install -D -m 0644 ${UNPACKDIR}/authorized-keys.conf \
        ${D}${sysconfdir}/ssh/sshd_config.d/10-raven.conf
    install -D -m 0644 ${UNPACKDIR}/overlay-storage.conf \
        ${D}${systemd_system_unitdir}/sshdgenkeys.service.d/10-raven.conf
}
