SUMMARY = "Raven development image with ROS 2 Jazzy"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb
inherit ros_distro_jazzy ros2_image

# Console image: retain hardware support, but no splash screen or desktop.
IMAGE_FEATURES:remove = "splash"
IMAGE_FEATURES += "ssh-server-openssh"
IMAGE_INSTALL:append = " ros-base demo-nodes-cpp iproute2 ethtool swupdate"
