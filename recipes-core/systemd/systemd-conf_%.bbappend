FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"
SRC_URI:append:raven-frdm-imx95 = " file://30-raven-watchdog.conf file://80-raven-wifi.network"

FILES:${PN}:append:raven-frdm-imx95 = " ${sysconfdir}/systemd/network/80-raven-wifi.network"
CONFFILES:${PN}:append:raven-frdm-imx95 = " ${sysconfdir}/systemd/network/80-raven-wifi.network"

do_install:append:raven-frdm-imx95() {
    install -D -m 0644 ${UNPACKDIR}/30-raven-watchdog.conf \
        ${D}${systemd_unitdir}/system.conf.d/30-raven-watchdog.conf
    install -D -m 0644 ${UNPACKDIR}/80-raven-wifi.network \
        ${D}${sysconfdir}/systemd/network/80-raven-wifi.network
}
