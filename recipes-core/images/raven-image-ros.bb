SUMMARY = "Raven development image with ROS 2 Jazzy"
LICENSE = "MIT"

require recipes-core/images/core-image-minimal.bb
inherit ros_distro_jazzy ros2_image

# Serial console and an SSH server for development, without a desktop.
IMAGE_FEATURES += "ssh-server-openssh"
IMAGE_INSTALL:append = " ros-base demo-nodes-cpp iproute2 ethtool"
