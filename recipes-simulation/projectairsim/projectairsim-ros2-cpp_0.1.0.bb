SUMMARY = "Upstream Project AirSim C++ ROS 2 sensor bridge"
DESCRIPTION = "Simulator-owned ROS 2 camera, IMU, clock and reference-pose publishers for on-target evaluation"
HOMEPAGE = "https://github.com/iamaisim/ProjectAirSim"
BUGTRACKER = "https://github.com/iamaisim/ProjectAirSim/issues"
SECTION = "devel"
LICENSE = "MIT & BSL-1.0"
LIC_FILES_CHKSUM = "\
    file://../../docs/license.md;md5=e848d080178fb2d08b67acc5ba80b9fd \
    file://${UNPACKDIR}/nng/LICENSE.txt;md5=a41e579bb4326c21c774f8e51e41d8a3 \
    file://${UNPACKDIR}/msgpack/LICENSE_1_0.txt;md5=e4224ccaecb14d942c71d31bef20d78c \
"
DEPENDS = "\
    action-msgs \
    ament-cmake-native \
    geometry-msgs \
    libeigen \
    nav-msgs \
    rclcpp \
    rclcpp-action \
    rosgraph-msgs \
    rosidl-default-generators-native \
    rosidl-default-runtime \
    rosidl-runtime-cpp \
    sensor-msgs \
    std-msgs \
    tf2-ros \
"

PROJECTAIRSIM_JSON_URI = "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"

# Pin the SDK's declared dependencies through BitBake, not configure-time downloads.
SRC_URI = "\
    git://github.com/iamaisim/ProjectAirSim.git;protocol=https;branch=main;name=airsim;destsuffix=airsim \
    git://github.com/nanomsg/nng.git;protocol=https;nobranch=1;name=nng;destsuffix=nng \
    git://github.com/msgpack/msgpack-c.git;protocol=https;nobranch=1;name=msgpack;destsuffix=msgpack \
    ${PROJECTAIRSIM_JSON_URI};name=json;subdir=json;downloadfilename=${BP}-json.hpp \
    file://0001-map-sensor-clock-epoch.patch;patchdir=../../ \
"
SRCREV_airsim = "09755454b8d82b0231af7e59fd1ab8f980ff32c5"
SRCREV_nng = "84aa701f42968bf536141a24e826da66963b571a"
SRCREV_msgpack = "8c602e8579c7e7d65d6f9c6703c9699db3fb0488"
SRCREV_FORMAT = "airsim_nng_msgpack"
SRC_URI[json.sha256sum] = "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6"
S = "${UNPACKDIR}/airsim/ros/projectairsim_ros2_cpp"

# The SDK and transport sources are siblings of the ROS package source directory.
DEBUG_PREFIX_MAP:append = " -ffile-prefix-map=${UNPACKDIR}=/usr/src/debug/${PN}/${PV}"

ROS_BPN = "projectairsim_ros2_cpp"
inherit ros_distro_jazzy ros_component ros_ament_cmake

EXTRA_OECMAKE += "\
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_MSGPACK=${UNPACKDIR}/msgpack \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN-JSON=${UNPACKDIR}/json \
    -DFETCHCONTENT_SOURCE_DIR_NNG=${UNPACKDIR}/nng \
    -DPROJECTAIRSIM_ROS2_CPP_ENABLE_UNREAL_TESTS=OFF \
"
RDEPENDS:${PN} += "\
    action-msgs \
    geometry-msgs \
    nav-msgs \
    rclcpp \
    rclcpp-action \
    rosgraph-msgs \
    rosidl-default-runtime \
    sensor-msgs \
    std-msgs \
    tf2-ros \
"

# Preserve the SDK include name while using a collision-free download filename.
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/json/${BP}-json.hpp ${UNPACKDIR}/json/json.hpp
}
