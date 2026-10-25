FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"

# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " file://0001-add-raven-frdm-imx95-board.patch"

# Expose the existing NXP watchdog driver's state and timeout for diagnostics.
SRC_URI:append:raven-frdm-imx95 = " file://raven-watchdog.cfg"

# NXP Robotics Linux commit d16e506cb9f21675d71a30b36a374405220f91a5.
# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " file://0002-nxp-rpmsg-tty2.patch file://raven-rpmsg.cfg"
KERNEL_MODULE_AUTOLOAD:append:raven-frdm-imx95 = " rpmsg_tty2"

require recipes-multimedia/imx477/imx477-source.inc

# nooelint: oelint.file.patchsignedoff Imported NXP patch; retain upstream authorship.
SRC_URI:append:raven-frdm-imx95 = " \
    file://0003-nxp-imx477-camera.patch \
    file://0004-raven-m7-ownership-and-camera-tree.patch \
    file://0005-imx477-advertise-sensor-modes.patch;apply=no \
    file://raven-camera.cfg \
"

IMX477_STAGE_FILES:raven-frdm-imx95 = "\
    linux-imx/imx477.c:drivers/media/i2c/imx477.c \
    linux-imx/imx95-15x15-frdm-imx477.dtso:arch/arm64/boot/dts/freescale/imx95-15x15-frdm-imx477.dtso \
"

# The imported camera-pack driver is outside the kernel Git index. Apply its
# patch after the kernel Git patch queue, using the staged source as the base.
do_patch:append:raven-frdm-imx95() {
    patch --batch --fuzz=0 --no-backup-if-mismatch -p1 -d "${S}" \
        -i "${UNPACKDIR}/0005-imx477-advertise-sensor-modes.patch"
}
