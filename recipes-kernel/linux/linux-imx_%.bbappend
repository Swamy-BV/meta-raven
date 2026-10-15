FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"

# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " file://0001-raven-m7-resource-ownership.patch"

# NXP Robotics Linux commit d16e506cb9f21675d71a30b36a374405220f91a5.
# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI:append:raven-frdm-imx95 = " file://0002-nxp-rpmsg-tty2.patch file://raven-rpmsg.cfg"
KERNEL_MODULE_AUTOLOAD:append:raven-frdm-imx95 = " rpmsg_tty2"
