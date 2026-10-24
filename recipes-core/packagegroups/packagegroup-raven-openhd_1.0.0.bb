SUMMARY = "Optional Raven OpenHD video and telemetry link"
DESCRIPTION = "Headless OpenHD backend with an independently selected external radio driver"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "connectivity"
LICENSE = "AGPL-3.0-only"

inherit packagegroup

VIRTUAL-RUNTIME_openhd-radio ?= "rtl8812au-openhd"
RDEPENDS:${PN} = "${VIRTUAL-RUNTIME_openhd-radio} openhd"
