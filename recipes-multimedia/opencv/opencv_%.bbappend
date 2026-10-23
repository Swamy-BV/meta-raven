# Raven is headless; OpenVINS uses cv::Mat and does not use OpenCL dispatch.
# Avoid the Mali/Vulkan dependency chain when those distro features are absent.
PACKAGECONFIG:remove:raven = "opencl"

# Performance test binaries are not part of the drone runtime.
EXTRA_OECMAKE:append:raven = " -DBUILD_PERF_TESTS=OFF"
