SUMMARY = "detection shared C++17 headers"
DESCRIPTION = "Separately packaged shared library with build-time quality checks"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "devel"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "nlohmann-json"

inherit cmake raven_source raven_quality

ALLOW_EMPTY:${PN} = "1"

BBCLASSEXTEND = "native nativesdk"
