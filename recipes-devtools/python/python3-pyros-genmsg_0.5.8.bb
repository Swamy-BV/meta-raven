SUMMARY = "ROS message generation library used by PX4"
DESCRIPTION = "Provides ROS message generation tools required by the PX4 firmware build"
HOMEPAGE = "https://github.com/ros/genmsg"
BUGTRACKER = "https://github.com/ros/genmsg/issues"
SECTION = "devel/python"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://package.xml;beginline=9;endline=9;md5=d566ef916e9dedc494f5f793a6690ba5"

PYPI_PACKAGE = "pyros_genmsg"
SRC_URI[sha256sum] = "3c1cb07d9c40f9e60872987eec983aac5bdb5920ea77980303fb1c46c308f478"

inherit pypi setuptools3
BBCLASSEXTEND = "native"
