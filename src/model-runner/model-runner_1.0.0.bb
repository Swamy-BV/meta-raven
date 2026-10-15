SUMMARY = "Model registry, selection and bounded local inference"
DESCRIPTION = "ROS RGB input, transactional model selection and isolated TensorFlow Lite and ONNX inference"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "multimedia"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "\
    ament-cmake-native \
    detection \
    gdk-pixbuf \
    glib-2.0 \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    local-ipc \
    microsoft-gsl \
    nlohmann-json \
    onnxruntime \
    platform-contracts \
    rclcpp \
    sensor-msgs \
    tensorflow-lite \
"
SRC_URI = "file://model-runner.conf file://model-runner-tmpfiles.conf file://model-runner.service"

ROS_BPN = "model_runner"
inherit ros_distro_jazzy ros_component ros_ament_cmake pkgconfig systemd useradd

SYSTEMD_SERVICE:${PN} = "model-runner.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 971 model-runner"
USERADD_PARAM:${PN} = "--system --uid 971 --gid model-runner --home-dir ${localstatedir}/lib/model-runner \
                       --no-create-home --shell /sbin/nologin model-runner"
CONFFILES:${PN} = "${sysconfdir}/model-runner.conf"
FILES:${PN} += "${nonarch_libdir}/tmpfiles.d/model-runner.conf"
RDEPENDS:${PN} = "\
    gdk-pixbuf-loader-bmp \
    model-runner-models \
    rclcpp \
    rmw-cyclonedds-cpp \
    sensor-msgs \
    tensorflow-lite-runtime \
"
RDEPENDS:${PN}:append:mx95-nxp-bsp = " \
    gstreamer1.0-plugins-bad-videoparsersbad \
    gstreamer1.0-plugins-base-app \
    gstreamer1.0-plugins-good-isomp4 \
    gstreamer1.0-plugins-good-video4linux2 \
    imx-gst1.0-plugin-video \
    model-yolov4-tiny-neutron \
    tensorflow-lite-neutron-delegate \
"

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/model-runner.conf ${D}${sysconfdir}/model-runner.conf
    install -D -m 0644 ${UNPACKDIR}/model-runner.service ${D}${systemd_system_unitdir}/model-runner.service
    sed -i 's|@ROS_PREFIX@|${ros_prefix}|g' ${D}${systemd_system_unitdir}/model-runner.service
    install -D -m 0644 ${UNPACKDIR}/model-runner-tmpfiles.conf ${D}${nonarch_libdir}/tmpfiles.d/model-runner.conf
}
inherit raven_source raven_quality
