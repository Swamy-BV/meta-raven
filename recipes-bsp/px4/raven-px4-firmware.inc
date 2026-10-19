require conf/distro/include/raven-px4.inc

LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=26fc10a6b30f158c0cd27e3a15215280"

DEPENDS += "\
    cmake-native \
    gcc-arm-none-eabi-native \
    ninja-native \
    patch-native \
    python3-empy-native \
    python3-jinja2-native \
    python3-jsonschema-native \
    python3-kconfiglib-native \
    python3-native \
    python3-packaging-native \
    python3-pyros-genmsg-native \
    python3-pyyaml-native \
"
SRC_URI = "gitsm://github.com/Swamy-BV/PX4-Autopilot.git;protocol=https;branch=develop;name=px4 \
           git://github.com/OpenAMP/libmetal.git;protocol=https;nobranch=1;name=libmetal;destsuffix=libmetal \
           git://github.com/OpenAMP/open-amp.git;protocol=https;nobranch=1;name=openamp;destsuffix=open-amp \
           git://github.com/eProsima/Micro-CDR.git;protocol=https;nobranch=1;name=microcdr;destsuffix=microcdr"
SRCREV_px4 = "${RAVEN_PX4_SRCREV}"
# Match the dependency revisions selected by NuttX's openamp/Makefile.
SRCREV_libmetal = "a4bce3507502a7eb9e29bafe0eb174ed5c4316e9"
SRCREV_openamp = "c468328487a1e0596307a5ef7172756819e15745"
SRCREV_microcdr = "3d1b17703c7cf4f22def2910bc845bdb5152d7b5"
SRCREV_FORMAT = "px4_libmetal_openamp_microcdr"
RAVEN_PX4_TARGET ?= "raven_frdm_default"
COMPATIBLE_MACHINE = "raven-frdm-imx95"
PACKAGE_ARCH = "${MACHINE_ARCH}"
INHIBIT_DEFAULT_DEPS = "1"

inherit deploy python3native

# gitsm fetches the commits recorded by PX4. Do not let PX4 fetch again at
# configure time, when BitBake disables network access.
export GIT_SUBMODULES_ARE_EVIL = "1"

# PX4 drives Ninja and NuttX's nested Make itself. Keep the outer Make serial
# and pass BitBake's parallelism setting directly to Ninja below.
# NuttX supplies its own rules; avoid GNU Make implicit-rule searches.
export MAKEFLAGS = "-r"
# The selected compiler is GCC; avoid recursive prefix detection in Make.
export DEFINE_PREFIX = "-D"
export INCDIR_PREFIX = "-I"
export INCSYSDIR_PREFIX = "-isystem "

do_configure() {
    cd ${S}/platforms/nuttx/NuttX/nuttx/openamp
    for library in libmetal open-amp; do
        rm -rf "$library"
        cp -R ${UNPACKDIR}/$library .
        # Reuse NuttX's patch order rather than maintaining another patch list.
        for fix in $(sed -n 's/.*patch -p0 < //p' $library.defs); do
            patch --batch -p0 < "$fix"
        done
    done
}

do_compile() {
    # The XRCE client clones Micro-CDR through ExternalProject. Redirect only
    # this task to BitBake's pinned checkout; never fetch from the network here.
    export GIT_CONFIG_COUNT=1
    export GIT_CONFIG_KEY_0="url.file://${UNPACKDIR}/microcdr.insteadOf"
    export GIT_CONFIG_VALUE_0="https://github.com/eProsima/Micro-CDR.git"
    oe_runmake -j1 -C ${S} BUILD_DIR_SUFFIX=_yocto \
        PX4_MAKE_ARGS="${@oe.utils.parallel_make_argument(d, '-j%d') or '-j1'}" ${RAVEN_PX4_TARGET}
}

do_install[noexec] = "1"

do_deploy() {
    install -D -m 0644 ${S}/build/${RAVEN_PX4_TARGET}_yocto/${RAVEN_PX4_TARGET}.bin \
        ${DEPLOYDIR}/mcore-demos/${RAVEN_PX4_TARGET}.bin
    install -D -m 0644 ${S}/build/${RAVEN_PX4_TARGET}_yocto/${RAVEN_PX4_TARGET}.elf \
        ${DEPLOYDIR}/${RAVEN_PX4_TARGET}.elf
}
addtask deploy after do_compile before do_build
