SUMMARY = "Raven Linux root filesystem SWUpdate bundle"
DESCRIPTION = "Builds an A/B rootfs update bundle from raven-image"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
PV = "0.1.0"

inherit swupdate

SRC_URI = "file://sw-description"

# Build the ROS image first and package its ext4 filesystem, not the factory WIC.
IMAGE_DEPENDS = "raven-image"
SWUPDATE_IMAGES = "raven-image"
SWUPDATE_IMAGES_FSTYPES[raven-image] = ".rootfs.ext4.gz"

COMPATIBLE_MACHINE = "raven-frdm-imx95"
