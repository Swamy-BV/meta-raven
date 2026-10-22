# Sparse camera-motion estimation needs image/feature math only. Capture,
# codecs, GUI and OpenCL stay in the existing media/NPU pipeline.
PACKAGECONFIG:remove:class-target:raven = "\
    dnn gapi gphoto2 gstreamer gtk jpeg libav libv4l opencl png python3 samples tbb tests text tiff v4l \
"
EXTRA_OECMAKE:append:class-target:raven = " -DBUILD_LIST=core,imgproc,video,calib3d -DBUILD_opencv_apps=OFF"
# The upstream installer still creates an empty executable directory.
FILES:${PN}-apps:append:class-target:raven = " ${bindir}"
