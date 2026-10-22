cmake_minimum_required(VERSION 3.20)
project(
  ByteTrack
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Eigen3 REQUIRED)
set(BYTETRACK_SOURCE_DIR
    ""
    CACHE PATH "Pinned and patched upstream ByteTrack C++ core")
if(NOT IS_DIRECTORY "${BYTETRACK_SOURCE_DIR}")
  message(FATAL_ERROR "BYTETRACK_SOURCE_DIR must contain the pinned C++ tracking core")
endif()

# Build only the CPU association/Kalman core; upstream retains its coding policy.
add_library(
  bytetrack SHARED
  ${BYTETRACK_SOURCE_DIR}/src/BYTETracker.cpp ${BYTETRACK_SOURCE_DIR}/src/STrack.cpp
  ${BYTETRACK_SOURCE_DIR}/src/kalmanFilter.cpp ${BYTETRACK_SOURCE_DIR}/src/utils.cpp
  ${BYTETRACK_SOURCE_DIR}/src/lapjv.cpp)
add_library(ByteTrack::ByteTrack ALIAS bytetrack)
set_target_properties(
  bytetrack
  PROPERTIES EXPORT_NAME ByteTrack
             VERSION ${PROJECT_VERSION}
             SOVERSION 1)
target_compile_features(bytetrack PUBLIC cxx_std_17)
target_include_directories(bytetrack SYSTEM PUBLIC $<BUILD_INTERFACE:${BYTETRACK_SOURCE_DIR}/include>
                                                   $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/bytetrack>)
target_link_libraries(bytetrack PUBLIC Eigen3::Eigen)

include(CTest)
if(BUILD_TESTING)
  add_executable(bytetrack-camera-motion-test tests/camera_motion.cpp)
  target_link_libraries(bytetrack-camera-motion-test PRIVATE ByteTrack::ByteTrack)
  add_test(NAME bytetrack-camera-motion COMMAND bytetrack-camera-motion-test)
endif()

install(
  TARGETS bytetrack
  EXPORT ByteTrackTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Exclude the demo's TensorRT logging header from the installed public API.
install(FILES ${BYTETRACK_SOURCE_DIR}/include/BYTETracker.h ${BYTETRACK_SOURCE_DIR}/include/STrack.h
              ${BYTETRACK_SOURCE_DIR}/include/kalmanFilter.h ${BYTETRACK_SOURCE_DIR}/include/dataType.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bytetrack)
set(BYTETRACK_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/ByteTrack)
install(
  EXPORT ByteTrackTargets
  NAMESPACE ByteTrack::
  DESTINATION ${BYTETRACK_CMAKE_DIR})
configure_package_config_file(ByteTrackConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/ByteTrackConfig.cmake
                              INSTALL_DESTINATION ${BYTETRACK_CMAKE_DIR})
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ByteTrackConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ByteTrackConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/ByteTrackConfigVersion.cmake
        DESTINATION ${BYTETRACK_CMAKE_DIR})
