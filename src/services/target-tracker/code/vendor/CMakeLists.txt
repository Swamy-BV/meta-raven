# Upstream sources retain their coding policy. Raven's adapter is checked by
# raven_quality; the vendor demo, detector and CUDA/TensorRT build are excluded.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(CMAKE_CXX_CLANG_TIDY "")
set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "")
set(CMAKE_CXX_CPPCHECK "")
if(NOT IS_DIRECTORY "${BYTETRACK_SOURCE_DIR}")
  message(FATAL_ERROR "BYTETRACK_SOURCE_DIR must contain the pinned C++ tracking core")
endif()
add_library(
  bytetrack-core STATIC
  ${BYTETRACK_SOURCE_DIR}/src/BYTETracker.cpp ${BYTETRACK_SOURCE_DIR}/src/STrack.cpp
  ${BYTETRACK_SOURCE_DIR}/src/kalmanFilter.cpp ${BYTETRACK_SOURCE_DIR}/src/utils.cpp
  ${BYTETRACK_SOURCE_DIR}/src/lapjv.cpp)
target_include_directories(bytetrack-core SYSTEM PUBLIC ${BYTETRACK_SOURCE_DIR}/include)
target_link_libraries(bytetrack-core PUBLIC Eigen3::Eigen)
