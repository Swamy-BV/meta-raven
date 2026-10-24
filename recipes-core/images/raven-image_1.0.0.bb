SUMMARY = "Raven runtime image with ROS 2 Jazzy"
DESCRIPTION = "Console Linux image with ROS 2 and Raven runtime services"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "images"
# nooelint: oelint.file.requirenotfound Provided by OE-Core through BBPATH; validated by BitBake.
require recipes-core/images/core-image-base.bb
LICENSE = "AGPL-3.0-only"
inherit ros_distro_jazzy ros2_image

# Raven has one default ROS distribution; keep artifact names board-oriented.
ROS_IMAGE_BASENAME_APPEND = ""

# Console image: retain hardware support, but no splash screen or desktop.
IMAGE_FEATURES:remove = "splash"
IMAGE_FEATURES += "ssh-server-openssh read-only-rootfs"

# SWUpdate writes this filesystem directly; match the factory A/B slot sizes.
IMAGE_ROOTFS_SIZE = "${@str(int(d.getVar('RAVEN_ROOTFS_SIZE_MIB')) * 1024)}"
IMAGE_ROOTFS_MAXSIZE = "${IMAGE_ROOTFS_SIZE}"
IMAGE_ROOTFS_EXTRA_SPACE = "0"
# Select packagegroup-raven-openhd in local.conf to evaluate the alternative.
VIRTUAL-RUNTIME_raven-radio-link ?= "packagegroup-raven-wfb"
IMAGE_INSTALL:append = " \
    ros-base \
    iproute2 \
    ethtool \
    swupdate \
    swupdate-www \
    libubootenv-bin \
    raven-storage \
    raven-update-config \
    wpa-supplicant \
    kernel-module-rpmsg-tty2 \
    micro-xrce-dds-agent \
    mavlink-router \
    ${VIRTUAL-RUNTIME_raven-radio-link} \
    px4-msgs \
    media-capture \
    camera-mavlink \
    mediamtx \
    neo-ipa-uguzzi \
    kernel-module-imx477 \
    vision-engine \
    target-tracker \
    guidance \
    visual-odometry \
    px4-odometry \
"

# Publish a block map with the compressed factory WIC for bmaptool and UUU.
# The SWU bundle consumes the standalone filesystem image, not the factory WIC.
IMAGE_FSTYPES:append = " wic.bmap ext4.gz"

# The boot partition includes the M7 firmware alongside imx-boot.
do_image_wic[depends] += "raven-px4-firmware:do_deploy"
