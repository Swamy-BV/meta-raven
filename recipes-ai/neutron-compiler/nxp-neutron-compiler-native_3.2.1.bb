SUMMARY = "NXP Neutron model compiler for the i.MX95 NPU"
DESCRIPTION = "Native compiler pinned to the board's Neutron 3.2.1 runtime"
HOMEPAGE = "https://eiq.nxp.com/learning-hub/tools/neutronSdk/index.html"
BUGTRACKER = "https://community.nxp.com/t5/eIQ-Machine-Learning-Software/bd-p/eiq"
SECTION = "devel"
LICENSE = "Proprietary"
NXP_LICENSE_DIR = "eiq_neutron_sdk-${PV}.dist-info/licenses"
LIC_FILES_CHKSUM = "file://${NXP_LICENSE_DIR}/LA_OPT_NXP_Software_License.txt;md5=dad891a0e1a4cffc85a3bddb7d40414a"
NO_GENERIC_LICENSE[Proprietary] = "${NXP_LICENSE_DIR}/LA_OPT_NXP_Software_License.txt"

DEPENDS = "zlib-native"

# editorconfig-checker-disable
# Preserve the pinned upstream wheel URL; only its standalone compiler is installed.
SRC_URI = "https://eiq.nxp.com/repository/eiq-neutron-sdk/eiq_neutron_sdk-${PV}-cp310-cp310-manylinux_2_31_x86_64.whl;downloadfilename=${BPN}-${PV}.zip"
# editorconfig-checker-enable
SRC_URI[sha256sum] = "4e6ab983ae6470dace8480fd10862c34c557c455f6e8fca26450bc7bc17103c9"
S = "${UNPACKDIR}"

inherit_defer native

COMPATIBLE_HOST = "x86_64.*-linux"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/eiq_neutron_sdk/bin/neutron-compiler ${D}${bindir}/neutron-compiler
}
