SUMMARY = "Raven Linux A/B SWUpdate bundle"
DESCRIPTION = "Builds an A/B rootfs and boot-file update bundle from raven-image"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
PV = "0.1.0"

inherit swupdate

SRC_URI = "file://sw-description"

# Build the ROS image and kernel first; package boot files with the rootfs.
IMAGE_DEPENDS = "raven-image"
do_swuimage[depends] += "virtual/kernel:do_deploy"
SWUPDATE_IMAGES = "raven-image Image imx95-15x15-frdm.dtb"
SWUPDATE_IMAGES_FSTYPES[raven-image] = ".rootfs.ext4.gz"

COMPATIBLE_MACHINE = "raven-frdm-imx95"
