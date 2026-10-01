FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# NXP's SWUpdate U-Boot patch targets the 19x19 EVK; keep the FRDM port here.
SRC_URI:append:raven-frdm-imx95 = " file://0001-raven-frdm-redundant-env-ab-boot.patch"
