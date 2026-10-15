# Keep NXP's Neo ISP pipeline and IPA; enable only the headless GStreamer adapter.
PACKAGECONFIG:remove:raven = "dng kms python pycamera opengl sdl"
PACKAGECONFIG:append:raven = " gst"
