FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"
SRC_URI:append:raven-frdm-imx95 = " file://raven-wifi.conf"

PACKAGE_ARCH:raven-frdm-imx95 = "${MACHINE_ARCH}"
SYSTEMD_SERVICE:${PN}:raven-frdm-imx95 = "wpa_supplicant@mlan0.service"
SYSTEMD_AUTO_ENABLE:${PN}:raven-frdm-imx95 = "enable"
RDEPENDS:${PN}:append:raven-frdm-imx95 = " iw raven-storage"

do_install:append:raven-frdm-imx95() {
    install -D -m 0644 ${UNPACKDIR}/raven-wifi.conf \
        ${D}${systemd_system_unitdir}/wpa_supplicant@mlan0.service.d/10-raven.conf
    install -d -m 0700 ${D}${localstatedir}/lib/wpa_supplicant
}
