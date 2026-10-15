cmake_minimum_required(VERSION 3.20)
project(
  media_capture
  VERSION 0.4.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(image_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(PlatformContracts REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(
  GSTREAMER
  REQUIRED
  IMPORTED_TARGET
  gstreamer-1.0
  gstreamer-app-1.0
  gstreamer-video-1.0
  gio-unix-2.0
  gdk-pixbuf-2.0)
add_library(media-pipeline STATIC pipeline_graph.cpp)
target_link_libraries(media-pipeline PRIVATE PkgConfig::GSTREAMER)
add_executable(media-capture main.cpp adapters/gstreamer/capture.cpp adapters/ros/node.cpp adapters/ros/settings.cpp)
target_link_libraries(media-capture media-pipeline PkgConfig::GSTREAMER Raven::platform-contracts)
ament_target_dependencies(
  media-capture
  SYSTEM
  rclcpp
  rcl_interfaces
  image_transport
  sensor_msgs
  std_srvs)
install(TARGETS media-capture RUNTIME DESTINATION lib/${PROJECT_NAME})

ament_package()
