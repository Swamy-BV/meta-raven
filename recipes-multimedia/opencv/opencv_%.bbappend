# Enable the transparent OpenCL API for Mali image tracking without a GUI.
PACKAGECONFIG:append:imxmali = " opencl"

# Performance test binaries are not part of the drone runtime.
EXTRA_OECMAKE:append:raven = " -DBUILD_PERF_TESTS=OFF"
