SUMMARY = "RTL8812EU external USB radio driver for WFB-ng"
DESCRIPTION = "Upstream packet-injection driver built against the selected kernel without modifying its source"
HOMEPAGE = "https://github.com/svpcom/rtl8812eu"
BUGTRACKER = "https://github.com/svpcom/rtl8812eu/issues"
SECTION = "kernel/modules"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://include/osdep_service.h;beginline=1;endline=14;md5=d1415c223f48d77507154f01e7fa002f"
SRC_URI = "git://github.com/svpcom/rtl8812eu.git;protocol=https;branch=v5.15.0.1"
SRCREV = "48e6e449e089fa954e4e15079bd864039e2960da"

inherit module

EXTRA_OEMAKE += "\
    CONFIG_PLATFORM_I386_PC=n \
    CONFIG_PLATFORM_ARM64_RPI=y \
    ARCH=${ARCH} \
    CROSS_COMPILE=${TARGET_PREFIX} \
    KSRC=${STAGING_KERNEL_BUILDDIR} \
    KVER=${KERNEL_VERSION} \
    USER_MODULE_NAME=8812eu \
"

do_install() {
    install -D -m 0644 ${B}/8812eu.ko \
        ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/8812eu.ko
}
