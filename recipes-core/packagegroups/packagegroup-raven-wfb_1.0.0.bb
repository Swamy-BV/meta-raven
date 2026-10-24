SUMMARY = "Optional Raven WFB-ng video and telemetry link"
DESCRIPTION = "Select the external radio driver independently of the WFB-ng userspace stack and NXP kernel source"
HOMEPAGE = "https://github.com/Swamy-BV/meta-raven"
BUGTRACKER = "https://github.com/Swamy-BV/meta-raven/issues"
SECTION = "connectivity"
LICENSE = "AGPL-3.0-only"

inherit packagegroup

# Override in local.conf or a machine/distro configuration. Use rtl8812eu-wfb
# for the EU module, or an empty value for a software-only evaluation image.
VIRTUAL-RUNTIME_wfb-radio ?= "rtl8812au-wfb"
RDEPENDS:${PN} = "${VIRTUAL-RUNTIME_wfb-radio} wfb-ng"
