SUMMARY = "Onboard ROS middleware configuration"
DESCRIPTION = "Common local DDS discovery for ROS services and interactive ROS environments"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "base"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "ament-cmake-native"
RDEPENDS:${PN} = "rmw-cyclonedds-cpp"
ROS_BPN = "raven_ros_config"
inherit ros_distro_jazzy ros_component ros_ament_cmake raven_source allarch
EXTRA_OECMAKE += "-DROS_CONFIG_DIR=${sysconfdir}/ros"
CONFFILES:${PN} = "${sysconfdir}/ros/cyclonedds.xml ${sysconfdir}/ros/ros2.env"
