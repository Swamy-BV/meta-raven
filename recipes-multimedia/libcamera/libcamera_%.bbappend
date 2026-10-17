require recipes-multimedia/imx477/imx477-source.inc

FILESEXTRAPATHS:prepend:raven-frdm-imx95 := "${THISDIR}/files:"
# nooelint: oelint.file.patchsignedoff Imported NXP patch; retain upstream authorship.
SRC_URI:append:raven-frdm-imx95 = " \
    file://0001-nxp-imx477-isp.patch \
    file://0002-reap-owned-ipa-workers.patch \
"

# Stage before patch/configure, rather than modifying sources during compilation.
IMX477_STAGE_FILES:raven-frdm-imx95 = "\
    libcamera/camera_helper_imx477.cpp:src/ipa/nxp/cam_helper/camera_helper_imx477.cpp \
    libcamera/imx477.yaml:src/ipa/nxp/neo/data/imx477.yaml \
"

# Keep NXP's Neo ISP pipeline and IPA; enable only the headless GStreamer adapter.
PACKAGECONFIG:remove:raven = "dng kms python pycamera opengl sdl"
PACKAGECONFIG:append:raven = " gst"
