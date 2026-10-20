SUMMARY = "Raven ROS 2 service interfaces"
DESCRIPTION = "ROS messages, services and actions for camera control, capture state and target selection"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "devel"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "action-msgs ament-cmake-native rosidl-default-generators-native rosidl-default-runtime std-msgs"
RDEPENDS:${PN} = "action-msgs rosidl-default-runtime std-msgs"
ROS_BPN = "raven_interfaces"
# rosidl owns generated code; there is no handwritten C++ in this package.
inherit ros_distro_jazzy ros_component ros_ament_cmake raven_source
