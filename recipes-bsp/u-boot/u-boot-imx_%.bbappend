FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"

# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " \
    file://0001-add-raven-frdm-imx95-board.patch \
    file://0002-raven-boot-policy-and-m7-ownership.patch \
    file://fw_env.config \
"

# Standard U-Boot environment package; both libubootenv and SWUpdate use this name.
UBOOT_INITIAL_ENV:raven-frdm-imx95 = "u-boot-initial-env"
