SUMMARY:${PN}-video = "NXP G2D video conversion and SoC feature plugins"

# Headless video conversion uses the DPU G2D library, without the OpenCL converter.
# nooelint: oelint.vars.mispell.unknown Defined by NXP's imx-gst1.0-plugin recipe.
DEPENDS_IMX_OPENCL_CONVERTER:raven = ""
DEPENDS:append:raven = " virtual/libg2d"

PACKAGES:prepend:raven = "${PN}-video "
FILES:${PN}-video:append = " \
    ${libdir}/gstreamer-1.0/libgstimxvideoconvert.so \
    ${libdir}/gstreamer-1.0/libgstimxsocfeaturesplugin.so \
    ${datadir}/imx_soc_features.ini \
"
