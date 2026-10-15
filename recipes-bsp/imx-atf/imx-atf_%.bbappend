FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"

# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " file://0001-raven-reserve-gpio2-for-m7.patch"

# Restart all domains through SM so Linux reboot reloads the boot container.
EXTRA_OEMAKE:append:raven-frdm-imx95 = " SYS_PWR_FULL_CTRL=1"

# TF-A does not track changes to Make command-line configuration in its objects.
# Rebuild both variants so SYS_PWR_FULL_CTRL takes effect on incremental builds.
do_compile:prepend:raven-frdm-imx95() {
    rm -rf ${S}/build/${ATF_PLATFORM} ${S}/build-ci/${ATF_PLATFORM}
}
