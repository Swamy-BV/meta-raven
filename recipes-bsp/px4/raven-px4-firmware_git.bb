SUMMARY = "Raven FRDM-IMX95 Cortex-M7 PX4 firmware"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=26fc10a6b30f158c0cd27e3a15215280"

SRC_URI = "gitsm://github.com/Swamy-BV/PX4-Autopilot.git;protocol=https;branch=develop"
SRCREV = "fda7e072f70b630d29f5290ff11d59fb81744836"

COMPATIBLE_MACHINE = "raven-frdm-imx95"
PACKAGE_ARCH = "${MACHINE_ARCH}"
INHIBIT_DEFAULT_DEPS = "1"

DEPENDS = "gcc-arm-none-eabi-native cmake-native ninja-native python3-native \
           python3-empy-native python3-jinja2-native python3-kconfiglib-native \
           python3-pyyaml-native python3-packaging-native python3-jsonschema-native \
           python3-pyros-genmsg-native"

inherit deploy python3native

# gitsm fetches the commits recorded by PX4. Do not let PX4 fetch again at
# configure time, when BitBake disables network access.
export GIT_SUBMODULES_ARE_EVIL = "1"

# PX4 drives Ninja and NuttX's nested Make itself. Avoid passing GNU Make's
# jobserver through those layers; bound Ninja directly instead.
PARALLEL_MAKE = ""

do_configure[noexec] = "1"

do_compile() {
    oe_runmake -C ${S} PX4_MAKE_ARGS=-j2 raven_frdm_default
}

do_install[noexec] = "1"

do_deploy() {
    install -D -m 0644 ${S}/build/raven_frdm_default/raven_frdm_default.bin \
        ${DEPLOYDIR}/mcore-demos/raven_frdm_default.bin
    install -D -m 0644 ${S}/build/raven_frdm_default/raven_frdm_default.elf \
        ${DEPLOYDIR}/raven_frdm_default.elf
}
addtask deploy after do_compile before do_build
