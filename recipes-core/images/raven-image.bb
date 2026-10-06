SUMMARY = "Raven development image with ROS 2 Jazzy"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb
inherit ros_distro_jazzy ros2_image

# Raven has one default ROS distribution; keep artifact names board-oriented.
ROS_IMAGE_BASENAME_APPEND = ""

# Console image: retain hardware support, but no splash screen or desktop.
IMAGE_FEATURES:remove = "splash"
IMAGE_FEATURES += "ssh-server-openssh"
IMAGE_INSTALL:append = " ros-base demo-nodes-cpp iproute2 ethtool swupdate"

# Publish a block map with the compressed factory WIC for bmaptool and UUU.
# The SWU bundle consumes the standalone filesystem image, not the factory WIC.
IMAGE_FSTYPES:append = " wic.bmap ext4.gz"

# The boot partition includes the M7 firmware alongside imx-boot.
do_image_wic[depends] += "raven-px4-firmware:do_deploy"
