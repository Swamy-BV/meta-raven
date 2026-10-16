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
IMAGE_FEATURES += "ssh-server-openssh"
IMAGE_INSTALL:append = " \
    ros-base \
    iproute2 \
    ethtool \
    swupdate \
    swupdate-www \
    libubootenv-bin \
    raven-update-config \
    kernel-module-rpmsg-tty2 \
    micro-xrce-dds-agent \
    mavlink-router \
    px4-msgs \
    perception-hub \
    media-capture \
    neo-ipa-uguzzi \
    kernel-module-imx477 \
    model-runner \
"

# Publish a block map with the compressed factory WIC for bmaptool and UUU.
# The SWU bundle consumes the standalone filesystem image, not the factory WIC.
IMAGE_FSTYPES:append = " wic.bmap ext4.gz"

# The boot partition includes the M7 firmware alongside imx-boot.
do_image_wic[depends] += "raven-px4-firmware:do_deploy"
