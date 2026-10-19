FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"
SRC_URI:append:raven-frdm-imx95 = " file://persistent-storage.conf"

# Device identity and authorized keys survive root filesystem replacement.
OPENSSH_HOST_KEY_DIR:raven-frdm-imx95 = "/persistent/ssh"
OPENSSH_HOST_KEY_DIR_READONLY_CONFIG:raven-frdm-imx95 = "/persistent/ssh"

do_install:append:raven-frdm-imx95() {
    sed -i 's|^AuthorizedKeysFile.*|AuthorizedKeysFile /persistent/ssh/%u/authorized_keys|' \
        ${D}${sysconfdir}/ssh/sshd_config ${D}${sysconfdir}/ssh/sshd_config_readonly
    install -D -m 0644 ${UNPACKDIR}/persistent-storage.conf \
        ${D}${systemd_system_unitdir}/sshdgenkeys.service.d/10-raven.conf
}
