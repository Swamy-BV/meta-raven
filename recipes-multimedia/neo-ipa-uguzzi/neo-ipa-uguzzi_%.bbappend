require recipes-multimedia/imx477/imx477-source.inc

FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"
# nooelint: oelint.file.patchsignedoff Imported NXP patch; retain upstream authorship.
SRC_URI:append:raven-frdm-imx95 = " file://0001-nxp-imx477-isp.patch"

# Stage before patch/configure, rather than modifying sources during compilation.
IMX477_STAGE_FILES:raven-frdm-imx95 = "\
    neo-ipa-uguzzi/camera_helper_imx477.cpp:cam_helper/camera_helper_imx477.cpp \
    neo-ipa-uguzzi/database_imx477_3840_2160.bin:data/database_imx477_3840_2160.bin \
"
