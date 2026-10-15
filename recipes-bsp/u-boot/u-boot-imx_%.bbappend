FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"

# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " \
    file://0001-raven-ab-boot.patch \
    file://0002-raven-frdm-gpio2-m7.patch \
    file://0003-raven-frdm-unknown-revision-power.patch \
"

# Match NXP Robotics: keep SPL code, stack and BSS outside the M7 OCRAM heap.
SRC_URI:append:raven-frdm-imx95 = " file://spl-ddr.cfg"

# Map the DDR SPL execution window before enabling its MMU.
# nooelint: oelint.file.patchsignedoff Bring-up patch has no human DCO sign-off.
SRC_URI:append:raven-frdm-imx95 = " file://0004-raven-spl-ddr-mmu-map.patch"
