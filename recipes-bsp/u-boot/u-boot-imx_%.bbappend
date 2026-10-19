FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"

# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " \
    file://0001-add-raven-frdm-imx95-board.patch \
    file://0002-raven-boot-policy-and-m7-ownership.patch \
    file://fw_env.config \
"

# Standard U-Boot environment package; both libubootenv and SWUpdate use this name.
UBOOT_INITIAL_ENV:raven-frdm-imx95 = "u-boot-initial-env"

# Export only managed boot scripts, never device identity or the current slot.
# SWUpdate's standard bootloader handler merges these after payload verification.
do_deploy:append:raven-frdm-imx95() {
    sed -n -E '/^(adjustbootsource|altbootcmd|bootcmd|bootlimit|bsp_bootcmd)=.+/p' \
        ${DEPLOYDIR}/${UBOOT_INITIAL_ENV}-${MACHINE}-${UBOOT_CONFIG} > ${DEPLOYDIR}/raven-boot.env
    test "$(wc -l < ${DEPLOYDIR}/raven-boot.env)" -eq 5 || bbfatal "Incomplete Raven boot environment"
}
