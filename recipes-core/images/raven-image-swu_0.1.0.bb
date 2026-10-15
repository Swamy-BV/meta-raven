SUMMARY = "Raven Linux A/B SWUpdate bundle"
DESCRIPTION = "Builds an A/B rootfs and boot-file update bundle from raven-image"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "images"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
PV = "0.1.0"

inherit swupdate

SRC_URI = "file://sw-description"

# Build the ROS image and kernel first; package boot files with the rootfs.
IMAGE_DEPENDS = "raven-image"
do_swuimage[depends] += "virtual/kernel:do_deploy"
SWUPDATE_IMAGES = "raven-image Image imx95-15x15-frdm.dtb"
SWUPDATE_IMAGES_FSTYPES[raven-image] = ".rootfs.ext4.gz"

COMPATIBLE_MACHINE = "raven-frdm-imx95"
