SUMMARY = "RTL8812AU external USB radio driver for OpenHD"
DESCRIPTION = "OpenHD packet-injection driver built against the selected kernel as a separate module"
HOMEPAGE = "https://github.com/OpenHD/rtl8812au"
BUGTRACKER = "https://github.com/OpenHD/rtl8812au/issues"
SECTION = "kernel/modules"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b234ee4d69f5fce4486a80fdaf4a4263"
SRC_URI = "git://github.com/OpenHD/rtl8812au.git;protocol=https;branch=v5.6.4.2"
SRCREV = "e77d52ea13b281faa25da7950bbc9fadc605d60c"

inherit module

EXTRA_OEMAKE += "\
    CONFIG_PLATFORM_I386_PC=n \
    CONFIG_PLATFORM_ARM64_RPI=y \
    ARCH=${ARCH} \
    CROSS_COMPILE=${TARGET_PREFIX} \
    KSRC=${STAGING_KERNEL_BUILDDIR} \
    KVER=${KERNEL_VERSION} \
"

do_install() {
    install -D -m 0644 ${B}/88XXau_ohd.ko \
        ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/88XXau_ohd.ko
}
