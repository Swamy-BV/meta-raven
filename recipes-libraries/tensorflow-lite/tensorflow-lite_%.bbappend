# CPU baseline keeps the drone image independent of OpenCL/GPU display dependencies.
PACKAGECONFIG:remove:raven = "gpu-delegate"
# nooelint: oelint.vars.mispell.unknown Defined by the upstream TensorFlow Lite recipe.
RDEPENDS_OPENCL:raven = ""

# The C++ service needs the shared interpreter, not the vendor Python examples.
# Preserve the upstream package for consumers that explicitly request it.
PACKAGES:prepend:raven = "${PN}-runtime "
FILES:${PN}-runtime:append:raven = " ${libdir}/libtensorflow-lite.so.*"
RDEPENDS:${PN}:append:raven = " ${PN}-runtime"
