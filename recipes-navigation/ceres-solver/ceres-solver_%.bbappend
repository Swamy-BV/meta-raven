# OpenVINS initialization uses DENSE_SCHUR with Ceres' Eigen backend.
# Avoid optional sparse/BLAS dependencies in the embedded navigation runtime.
PACKAGECONFIG:remove:raven = "suitesparse cxsparse lapack"
