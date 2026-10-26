SUMMARY = "Browser controller for selected-target following"
DESCRIPTION = "WebRTC video with acknowledged ROS target selection through the standard rosbridge server"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "robotics"
LICENSE = "AGPL-3.0-only & MIT"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
# nooelint: oelint.var.licenseremotefile The pinned WebRTC reader has its own upstream MIT license.
LIC_FILES_CHKSUM += "file://${UNPACKDIR}/reader-license/mediamtx-reader.LICENSE;md5=77fd2623bd5398430be5ce60489c2e81"
READER_VERSION = "1.20.1"
READER_BASE_URI = "https://raw.githubusercontent.com/bluenviron/mediamtx/v${READER_VERSION}"
# nooelint: oelint.vars.downloadfilename Reader artifacts use the separately pinned READER_VERSION.
SRC_URI = "\
    file://controller-bridge.service \
    file://controller-bridge.yaml \
    ${READER_BASE_URI}/internal/servers/webrtc/reader.js;name=reader;downloadfilename=mediamtx-reader.js \
    ${READER_BASE_URI}/LICENSE;name=readerlicense;subdir=reader-license;downloadfilename=mediamtx-reader.LICENSE \
"
SRC_URI[reader.sha256sum] = "a802f229b803c33713d4c69c4cc0d480108a5bf384947aeee4aaf04268bf85c1"
SRC_URI[readerlicense.sha256sum] = "ecae73b0a23185a35a1222edc0aae5245bb9739420bad0125b0c42e935301a80"
inherit allarch python3-dir raven_source systemd

RDEPENDS:${PN} = "\
    diagnostic-msgs \
    platform-interfaces \
    raven-ros-config \
    rosbridge-server \
    sensor-msgs \
    vision-msgs \
"
SYSTEMD_SERVICE:${PN} = "controller-bridge.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
CONFFILES:${PN} = "${sysconfdir}/controller-bridge.yaml"

do_install() {
    install -d ${D}${datadir}/raven-controller
    install -m 0644 ${S}/*.js ${S}/index.html ${S}/controller.css ${D}${datadir}/raven-controller/
    install -m 0644 ${UNPACKDIR}/mediamtx-reader.js ${D}${datadir}/raven-controller/reader.js
    install -m 0644 ${UNPACKDIR}/reader-license/mediamtx-reader.LICENSE ${D}${datadir}/raven-controller/
    install -D -m 0644 ${UNPACKDIR}/controller-bridge.yaml ${D}${sysconfdir}/controller-bridge.yaml
    install -D -m 0644 ${UNPACKDIR}/controller-bridge.service \
        ${D}${systemd_system_unitdir}/controller-bridge.service
    sed -i 's|@PYTHON_DIR@|${PYTHON_DIR}|g' ${D}${systemd_system_unitdir}/controller-bridge.service
}
