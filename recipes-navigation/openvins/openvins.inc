HOMEPAGE = "https://docs.openvins.com/"
BUGTRACKER = "https://github.com/rpng/open_vins/issues"
SECTION = "navigation"
LICENSE = "GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://../LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"

SRC_URI = "\
    git://github.com/rpng/open_vins.git;protocol=https;branch=master;destsuffix=git \
    file://0001-use-toolchain-flags-and-cxx17.patch;patchdir=.. \
"
SRCREV = "69488123ed9362dd44b6f28e7f4680abbff1442b"
S = "${UNPACKDIR}/git/${ROS_BPN}"

inherit ros_distro_jazzy ros_component ros_ament_cmake

EXTRA_OECMAKE += "\
    -DCMAKE_DISABLE_FIND_PACKAGE_catkin=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DDISABLE_MATPLOTLIB=ON \
    -DENABLE_ARUCO_TAGS=OFF \
    -DENABLE_ROS=ON \
"
