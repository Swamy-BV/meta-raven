SUMMARY = "MAVSDK's pinned MAVLink C headers and message definitions"
DESCRIPTION = "Builds MAVSDK-compatible MAVLink C headers and installs dialect definitions"
HOMEPAGE = "https://github.com/mavlink/mavlink"
BUGTRACKER = "https://github.com/mavlink/mavlink/issues"
SECTION = "libs"
LICENSE = "MIT & LGPL-3.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=54ad3cbe91bebcf6b1823970ff1fb97f"
DEPENDS = "python3-future-native python3-lxml-native"
# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI = "gitsm://github.com/mavlink/mavlink.git;protocol=https;nobranch=1 \
           file://0001-mavlink-use-staged-python-dependencies.patch"
SRCREV = "d6a7eeaf43319ce6da19a1973ca40180a4210643"
inherit cmake python3native
EXTRA_OECMAKE = "-DMAVLINK_DIALECT=ardupilotmega -DMAVLINK_VERSION=2.0"
ALLOW_EMPTY:${PN} = "1"

do_install:append() {
    install -d ${D}${includedir}/mavlink/message_definitions/v1.0
    for dialect in minimal standard common ardupilotmega; do
        install -m 0644 ${S}/message_definitions/v1.0/$dialect.xml \
            ${D}${includedir}/mavlink/message_definitions/v1.0/
    done
}
