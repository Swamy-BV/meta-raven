SUMMARY = "Model registry, selection and bounded local inference"
DESCRIPTION = "ROS model selection, isolated accelerated inference and standard vision outputs"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    diagnostic-msgs \
    gdk-pixbuf \
    glib-2.0 \
    microsoft-gsl \
    nlohmann-json \
    onnxruntime \
    platform-contracts \
    rcl-interfaces \
    rclcpp \
    sensor-msgs \
    std-msgs \
    std-srvs \
    tensorflow-lite \
    vision-msgs \
"
SRC_URI = "\
    file://vision-engine.conf \
    file://vision-engine-tmpfiles.conf \
    file://vision-engine.service \
    file://vision-engine.yaml \
"

ROS_BPN = "vision_engine"
inherit ros_distro_jazzy ros_component ros_ament_cmake pkgconfig systemd useradd

SYSTEMD_SERVICE:${PN} = "vision-engine.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 971 vision-engine"
USERADD_PARAM:${PN} = "--system --uid 971 --gid vision-engine --home-dir ${localstatedir}/lib/vision-engine \
                       --no-create-home --shell /sbin/nologin vision-engine"
CONFFILES:${PN} = "${sysconfdir}/vision-engine.conf ${sysconfdir}/vision-engine.yaml"
FILES:${PN} += "${nonarch_libdir}/tmpfiles.d/vision-engine.conf"
RDEPENDS:${PN} = "\
    diagnostic-msgs \
    gdk-pixbuf-loader-bmp \
    raven-ros-config \
    rcl-interfaces \
    rclcpp \
    rmw-cyclonedds-cpp \
    sensor-msgs \
    std-msgs \
    std-srvs \
    tensorflow-lite-runtime \
    vision-engine-models \
    vision-msgs \
"
RDEPENDS:${PN}:append:mx95-nxp-bsp = " \
    model-yolov4-tiny-neutron \
    tensorflow-lite-neutron-delegate \
"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/vision-engine.conf ${D}${sysconfdir}/vision-engine.conf
    install -D -m 0644 ${UNPACKDIR}/vision-engine.yaml ${D}${sysconfdir}/vision-engine.yaml
    install -D -m 0644 ${UNPACKDIR}/vision-engine.service ${D}${systemd_system_unitdir}/vision-engine.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/vision-engine.service
    install -D -m 0644 ${UNPACKDIR}/vision-engine-tmpfiles.conf ${D}${nonarch_libdir}/tmpfiles.d/vision-engine.conf
}
inherit raven_source raven_quality
