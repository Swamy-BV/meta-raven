SUMMARY = "PX4 Micro XRCE-DDS companion agent"
DESCRIPTION = "Bridges the M7 serial XRCE session over RPMsg into Linux ROS 2 DDS"
HOMEPAGE = "https://github.com/eProsima/Micro-XRCE-DDS-Agent"
BUGTRACKER = "https://github.com/eProsima/Micro-XRCE-DDS-Agent/issues"
SECTION = "console/utils"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57"
DEPENDS = "fastcdr fastrtps spdlog"
SRC_URI = "git://github.com/eProsima/Micro-XRCE-DDS-Agent.git;protocol=https;nobranch=1 \
           file://px4-dds-agent.service file://99-px4-dds.rules \
           file://0001-format-endpoints-with-modern-fmt.patch"
SRCREV = "73622810d984349b80bbac0ef55fc0b694d62222"
ROS_BPN = "microxrcedds_agent"

inherit ros_distro_jazzy ros_component ros_ament_cmake systemd useradd

# Use Yocto dependencies; upstream's superbuild would fetch during configure.
EXTRA_OECMAKE += "-DUAGENT_SUPERBUILD=OFF \
                  -DUAGENT_USE_SYSTEM_FASTDDS=ON \
                  -DUAGENT_USE_SYSTEM_FASTCDR=ON \
                  -DUAGENT_USE_SYSTEM_LOGGER=ON \
                  -DUAGENT_CED_PROFILE=OFF \
                  -DUAGENT_P2P_PROFILE=OFF \
                  -DUAGENT_DISCOVERY_PROFILE=OFF \
                  -DUAGENT_SOCKETCAN_PROFILE=OFF \
                  -DUAGENT_BUILD_TESTS=OFF \
                  -DUAGENT_BUILD_USAGE_EXAMPLES=OFF"
# The agent has a versioned library; keep its linker symlink in -dev.
FILES_SOLIBSDEV = "${ros_libdir}/lib*${SOLIBSDEV}"
SYSTEMD_SERVICE:${PN} = "px4-dds-agent.service"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 975 px4-dds"
USERADD_PARAM:${PN} = "--system --uid 975 --gid px4-dds --home-dir ${localstatedir}/lib/px4-dds \
                       --no-create-home --shell /sbin/nologin px4-dds"
FILES:${PN}:append = " ${nonarch_base_libdir}/udev/rules.d/99-px4-dds.rules"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/px4-dds-agent.service ${D}${systemd_system_unitdir}/px4-dds-agent.service
    install -D -m 0644 ${UNPACKDIR}/99-px4-dds.rules ${D}${nonarch_base_libdir}/udev/rules.d/99-px4-dds.rules
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/px4-dds-agent.service
}
