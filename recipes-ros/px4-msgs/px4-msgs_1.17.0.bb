SUMMARY = "ROS 2 interfaces matching Raven PX4 firmware"
DESCRIPTION = "Upstream px4_msgs package populated from the exact firmware message and service definitions"
HOMEPAGE = "https://github.com/PX4/px4_msgs"
BUGTRACKER = "https://github.com/PX4/px4_msgs/issues"
SECTION = "devel"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=6f63d3ce5fdb6ff9577d66fa59838f30"
DEPENDS = "ament-cmake-native builtin-interfaces rosidl-default-generators-native service-msgs"
SRC_URI = "git://github.com/PX4/px4_msgs.git;protocol=https;branch=main;name=interfaces \
           git://github.com/Swamy-BV/PX4-Autopilot.git;protocol=https;branch=develop;name=firmware;destsuffix=firmware"
SRCREV_interfaces = "9c218766535ee5b59cfe0ace157721a9e12a2f8d"
SRCREV_firmware = "${RAVEN_PX4_SRCREV}"
SRCREV_FORMAT = "interfaces_firmware"
ROS_BPN = "px4_msgs"
RDEPENDS:${PN} = "builtin-interfaces rosidl-default-runtime"

require conf/distro/include/raven-px4.inc
inherit ros_distro_jazzy ros_component ros_ament_cmake

# Follow PX4's copy_to_ros_ws.sh: flatten versioned messages into px4_msgs/msg.
# Remove definitions from a newer px4_msgs checkout before copying the pinned set.
do_configure:prepend() {
    rm -f ${S}/msg/*.msg ${S}/srv/*.srv
    cp ${UNPACKDIR}/firmware/msg/*.msg ${S}/msg/
    cp ${UNPACKDIR}/firmware/msg/versioned/*.msg ${S}/msg/
    cp ${UNPACKDIR}/firmware/srv/*.srv ${S}/srv/
}

# Ship the firmware's authoritative topic map beside the generated interfaces.
do_install:append() {
    install -m 0644 ${UNPACKDIR}/firmware/src/modules/uxrce_dds_client/dds_topics.yaml \
        ${D}${ros_datadir}/${ROS_BPN}/dds_topics.yaml
}
