SUMMARY = "Local ROS shared-memory transport"
DESCRIPTION = "Iceoryx RouDi configuration and isolated service account for local Cyclone DDS image transport"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "base"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
SRC_URI = "\
    file://raven-ros-shm.service \
    file://roudi.toml \
"

S = "${UNPACKDIR}"

inherit ros_distro_jazzy ros_opt_prefix allarch systemd useradd

RDEPENDS:${PN} = "iceoryx-posh"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 980 ros-shm"
USERADD_PARAM:${PN} = "--system --uid 980 --gid ros-shm --no-create-home --shell /sbin/nologin ros-shm"
SYSTEMD_SERVICE:${PN} = "raven-ros-shm.service"
# Image services start RouDi through Requires; a ROS CLI needs no daemon.
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/ros/roudi.toml"

do_install() {
    install -D -m 0644 ${UNPACKDIR}/roudi.toml ${D}${sysconfdir}/ros/roudi.toml
    install -D -m 0644 ${UNPACKDIR}/raven-ros-shm.service ${D}${systemd_system_unitdir}/raven-ros-shm.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/raven-ros-shm.service
}
