SUMMARY = "Checked Neutron delegate lifecycle for accelerated inference"
DESCRIPTION = "Shared runtime rejects uncompiled CPU models and exposes actual delegated and residual graph counts"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "libs"
LICENSE = "AGPL-3.0-only"
# nooelint: oelint.var.licenseremotefile Local Raven sources use the common AGPL-3.0-only text.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/AGPL-3.0-only;md5=73f1eb20517c55bf9493b7dd6e480788"
DEPENDS = "tensorflow-lite"
RDEPENDS:${PN}-dev += "tensorflow-lite-dev"
RDEPENDS:${PN}:append:mx95-nxp-bsp = " tensorflow-lite-neutron-delegate"
inherit cmake raven_source raven_quality
