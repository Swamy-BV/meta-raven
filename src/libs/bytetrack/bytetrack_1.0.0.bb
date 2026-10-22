SUMMARY = "Bounded ByteTrack C++ tracking library"
DESCRIPTION = "Reusable acquisition-time tracking core with bounded storage, independent of ROS, OpenCV and TensorRT"
HOMEPAGE = "https://github.com/FoundationVision/ByteTrack"
BUGTRACKER = "https://github.com/FoundationVision/ByteTrack/issues"
SECTION = "libs"
LICENSE = "MIT"
# nooelint: oelint.var.licenseremotefile Upstream license is fetched with the pinned Git source outside S.
LIC_FILES_CHKSUM = "file://${UNPACKDIR}/bytetrack/LICENSE;md5=e3d0d8afca6872e02b102c0776327a07"
DEPENDS = "libeigen"
SRC_URI = "\
    git://github.com/FoundationVision/ByteTrack.git;protocol=https;branch=main;destsuffix=bytetrack \
    file://0001-bound-tracking-and-use-acquisition-time.patch;patchdir=${UNPACKDIR}/bytetrack \
    file://0002-recover-retained-identities-with-weak-observations.patch;patchdir=${UNPACKDIR}/bytetrack \
    file://0003-keep-track-category.patch;patchdir=${UNPACKDIR}/bytetrack \
    file://0004-inject-association-policy.patch;patchdir=${UNPACKDIR}/bytetrack \
"
SRCREV = "d1bf0191adff59bc8fcfeaa0b33d3d1642552a99"
BB_GIT_SHALLOW = "1"

inherit cmake raven_source

CXXFLAGS:append = " -ffile-prefix-map=${UNPACKDIR}/bytetrack=/usr/src/debug/${PN}/${PV}/bytetrack"
EXTRA_OECMAKE += "-DBYTETRACK_SOURCE_DIR=${UNPACKDIR}/bytetrack/deploy/TensorRT/cpp"
RDEPENDS:${PN}-dev += "libeigen-dev"
