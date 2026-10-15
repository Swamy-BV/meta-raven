SUMMARY = "MAVSDK C++ APIs for MAVLink systems"
DESCRIPTION = "Provides C++ APIs for telemetry, missions and vehicle control over MAVLink"
HOMEPAGE = "https://mavsdk.mavlink.io/"
BUGTRACKER = "https://github.com/mavlink/MAVSDK/issues"
SECTION = "libs"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE.md;md5=84b641454775df91a2bae8fdd450e2e9"
DEPENDS = "asio curl fmt libmavlike libtinyxml2 mavsdk-libevents mavsdk-mavlink nlohmann-json picosha2 xz"
# nooelint: oelint.file.patchsignedoff Imported/bring-up patches have no human DCO sign-off; preserve provenance.
SRC_URI = "\
    git://github.com/mavlink/MAVSDK.git;protocol=https;nobranch=1 \
    file://0001-mavsdk-find-lzma-through-cmake-module.patch \
    file://0002-mavsdk-avoid-empty-serial-read-spin.patch \
"
SRCREV = "92cd383a2f58117f99afda5c96d1439d60236faa"
OECMAKE_SOURCEPATH = "${S}/cpp"
inherit cmake
EXTRA_OECMAKE = "-DSUPERBUILD=OFF -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON -DBUILD_MAVSDK_SERVER=OFF -DCCACHE=OFF"

# Upstream derives its ABI version from git describe. BitBake need not fetch tags.
do_configure:prepend() {
    git -C ${S} tag -f v${PV} ${SRCREV}
}
