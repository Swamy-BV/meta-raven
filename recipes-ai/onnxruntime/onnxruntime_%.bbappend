FILESEXTRAPATHS:prepend:raven := "${THISDIR}/files:"

# nooelint: oelint.file.patchsignedoff Local fix pending upstream authorship review.
SRC_URI:append:raven = " \
    file://0001-neutron-preserve-matmul-row-sums-and-flush-input.patch \
    file://0002-neutron-reject-unsupported-asymmetric-weights.patch \
    file://0003-neutron-reject-failing-vision-matmul-shapes.patch \
"

# Native headless CPU runtime; inference applications remain C++17.
PACKAGECONFIG:remove:raven = "python neutron bmm"
EXTRA_OECMAKE:append:raven = " -Donnxruntime_BUILD_UNIT_TESTS=OFF"

# Replace the vendor append, which unconditionally installs test binaries and
# removes Python files even when those features are disabled. CMake installs
# the shared runtime and public headers. No Python examples enter our image.
# Snapshot preserves the upstream installer for other distros; the custom installer is scoped to Raven.
# nooelint: oelint.vars.noncoreoverride
ONNXRUNTIME_VENDOR_INSTALL := "${do_install}"
unset do_install
# nooelint: oelint.vars.noncoreoverride Restore upstream behavior outside Raven after clearing its append operations.
do_install = "${ONNXRUNTIME_VENDOR_INSTALL}"
do_install[func] = "1"
do_install[fakeroot] = "1"
do_install[cleandirs] = "${D}"
do_install[dirs] = "${B}"
do_install:raven() {
    cmake_do_install
    install -d ${D}${datadir}/licenses/${PN}
    install -m 0644 ${S}/LICENSE ${D}${datadir}/licenses/${PN}/LICENSE
}
FILES:${PN}:append:raven = " ${datadir}/licenses/${PN}"
