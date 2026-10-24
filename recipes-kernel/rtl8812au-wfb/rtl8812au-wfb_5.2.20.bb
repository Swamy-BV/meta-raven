SUMMARY = "RTL8812AU external USB radio driver for WFB-ng"
DESCRIPTION = "Upstream packet-injection driver built against the selected kernel without modifying its source"
HOMEPAGE = "https://github.com/svpcom/rtl8812au"
BUGTRACKER = "https://github.com/svpcom/rtl8812au/issues"
SECTION = "kernel/modules"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b234ee4d69f5fce4486a80fdaf4a4263"
SRC_URI = "git://github.com/svpcom/rtl8812au.git;protocol=https;branch=v5.2.20"
SRCREV = "6e75916416de1dce5ecd37f824896bebf96aaf8f"

inherit module

EXTRA_OEMAKE += "\
    CONFIG_PLATFORM_I386_PC=n \
    CONFIG_PLATFORM_ARM64_RPI=y \
    ARCH=${ARCH} \
    CROSS_COMPILE=${TARGET_PREFIX} \
    KSRC=${STAGING_KERNEL_BUILDDIR} \
    KVER=${KERNEL_VERSION} \
    USER_MODULE_NAME=88XXau \
"

# Upstream's install target runs host depmod and strips the object. Let
# BitBake package the unstripped module and generate target depmod metadata.
do_install() {
    install -D -m 0644 ${B}/88XXau_wfb.ko \
        ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/88XXau_wfb.ko
}
