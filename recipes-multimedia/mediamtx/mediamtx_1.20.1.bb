SUMMARY = "Media router with WebRTC browser viewing"
DESCRIPTION = "MediaMTX routes encoded RTP video to browser clients without transcoding"
HOMEPAGE = "https://mediamtx.org"
BUGTRACKER = "https://github.com/bluenviron/mediamtx/issues"
SECTION = "multimedia"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://src/${GO_IMPORT}/LICENSE;md5=77fd2623bd5398430be5ce60489c2e81"
# nooelint: oelint.var.licenseremotefile The separately fetched frontend license is outside the Go source tree.
LIC_FILES_CHKSUM += "file://${HLSJS_LICENSE};md5=8c22b1b3074f826155dbc85e6a0d4d1f"
DEPENDS = "unzip-native"
HLSJS_VERSION = "1.7.0"
HLSJS_LICENSE = "${UNPACKDIR}/hlsjs-license/hls.js-${PV}-${HLSJS_VERSION}.LICENSE"
HLSJS_ARCHIVE_URI = "https://github.com/video-dev/hls.js/releases/download/v${HLSJS_VERSION}/release.zip"
HLSJS_LICENSE_URI = "https://raw.githubusercontent.com/video-dev/hls.js/v${HLSJS_VERSION}/LICENSE"
SRC_URI = "\
    git://github.com/bluenviron/mediamtx.git;protocol=https;nobranch=1;destsuffix=${GO_SRCURI_DESTSUFFIX} \
    file://mediamtx.yml \
    file://mediamtx.service \
    file://60-mediamtx.conf \
    file://0001-allow-build-without-rpi-camera.patch;patchdir=src/${GO_IMPORT} \
    ${HLSJS_ARCHIVE_URI};name=hlsjs;subdir=hlsjs;downloadfilename=hls.js-${PV}-${HLSJS_VERSION}.zip \
    ${HLSJS_LICENSE_URI};name=hlslicense;subdir=hlsjs-license;downloadfilename=hls.js-${PV}-${HLSJS_VERSION}.LICENSE \
"
SRC_URI[hlsjs.sha256sum] = "e6f742917020e6f43b54270d71065763f2d92c4acf32dccf7dd76cb28832b2c3"
SRC_URI[hlslicense.sha256sum] = "ca8773cf798c7ed997d4dd7c8e23c348699f8d5b7462636694cc14de6cda12db"
SRCREV = "883194a19b7244355c9bc975c0574c9842733637"
S = "${UNPACKDIR}/source"
GO_IMPORT = "github.com/bluenviron/mediamtx"
GO_INSTALL = "${GO_IMPORT}"
GO_LINKSHARED = ""
GOBUILDFLAGS:append = " -tags=norpicamera"
inherit go-mod go-mod-update-modules systemd useradd

# nooelint: oelint.file.requireinclude OE's update_modules task regenerates these optional bootstrap includes.
include mediamtx-go-mods.inc
# nooelint: oelint.file.requireinclude OE's update_modules task regenerates these optional bootstrap includes.
include mediamtx-licenses.inc

SYSTEMD_SERVICE:${PN} = "mediamtx.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system --gid 977 mediamtx"
USERADD_PARAM:${PN} = "--system --uid 977 --gid mediamtx --no-create-home --shell /sbin/nologin mediamtx"
CONFFILES:${PN} = "${sysconfdir}/mediamtx.yml"

do_configure:append() {
    printf '%s' 'v${PV}' > ${S}/src/${GO_IMPORT}/internal/core/VERSION
    install -m 0644 ${UNPACKDIR}/hlsjs/dist/hls.min.js ${S}/src/${GO_IMPORT}/internal/servers/hls/
}

do_install:append() {
    install -D -m 0644 ${UNPACKDIR}/60-mediamtx.conf ${D}${sysconfdir}/sysctl.d/60-mediamtx.conf
    install -D -m 0644 ${UNPACKDIR}/mediamtx.yml ${D}${sysconfdir}/mediamtx.yml
    install -D -m 0644 ${UNPACKDIR}/mediamtx.service ${D}${systemd_system_unitdir}/mediamtx.service
}
