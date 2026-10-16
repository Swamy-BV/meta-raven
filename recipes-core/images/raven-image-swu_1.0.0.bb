SUMMARY = "Raven Linux A/B SWUpdate bundle"
DESCRIPTION = "Updates Linux A/B slots and the shared i.MX boot container with readback verification"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "images"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"

inherit swupdate

SRC_URI = "file://sw-description"

# The eMMC user-area boot container starts at IMX_BOOT_SEEK KiB. U-Boot's
# redundant environments start at 7 MiB; reject any container that overlaps them.
RAVEN_IMX_BOOT_IMAGE = "imx-boot-${MACHINE}-${UBOOT_CONFIG}.bin-${IMXBOOT_TARGETS}"
RAVEN_IMX_BOOT_OFFSET = "${@str(int(d.getVar('IMX_BOOT_SEEK')) * 1024)}"
RAVEN_IMX_BOOT_LIMIT = "7340032"
IMAGE_DEPENDS = "raven-image"
do_swuimage[depends] += "virtual/kernel:do_deploy imx-boot:do_deploy"
SWUPDATE_IMAGES = "raven-image Image ${RAVEN_CAMERA_DTB} ${RAVEN_IMX_BOOT_IMAGE}"
SWUPDATE_IMAGES_FSTYPES[raven-image] = ".rootfs.ext4.gz"

COMPATIBLE_MACHINE = "raven-frdm-imx95"

python do_swuimage:prepend() {
    import os
    filename = os.path.join(d.getVar('DEPLOY_DIR_IMAGE'), d.getVar('RAVEN_IMX_BOOT_IMAGE'))
    size = os.path.getsize(filename)
    offset = int(d.getVar('RAVEN_IMX_BOOT_OFFSET'))
    limit = int(d.getVar('RAVEN_IMX_BOOT_LIMIT'))
    if offset < 32768 or size <= 0 or offset + size > limit:
        bb.fatal('i.MX boot container exceeds the reserved eMMC boot area')
    d.setVar('RAVEN_IMX_BOOT_SIZE', str(size))
}
