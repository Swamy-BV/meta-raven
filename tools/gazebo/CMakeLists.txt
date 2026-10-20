# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.16)
project(
  gazebo_camera_stream
  VERSION 1.0.0
  LANGUAGES CXX)
# PX4 GstCameraSystem at fffec82b2f970e5fec94174d5e1289c55a4ac52f,
# Adapted for I420 hardware decode compatibility, sensor-rate caps and live PTS.
# Vendor sources retain their upstream BSD-3-Clause notices.
find_package(gz-sim8 REQUIRED)
find_package(gz-plugin2 REQUIRED COMPONENTS register)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0)
add_library(GstCameraSystem SHARED vendor/GstCameraSystem.cpp)
target_compile_features(GstCameraSystem PRIVATE cxx_std_17)
target_link_libraries(GstCameraSystem PRIVATE gz-sim8::gz-sim8 gz-plugin2::gz-plugin2 PkgConfig::GSTREAMER
                                              Threads::Threads)
