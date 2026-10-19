cmake_minimum_required(VERSION 3.20)
project(
  media_capture
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(image_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(raven_interfaces REQUIRED)
find_package(diagnostic_msgs REQUIRED)
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
add_executable(
  media-capture
  main.cpp
  core/validation.cpp
  application/session.cpp
  adapters/gstreamer/backend.cpp
  adapters/gstreamer/pipeline.cpp
  adapters/storage/photo_store.cpp
  adapters/ros/node.cpp
  adapters/ros/settings.cpp)
target_link_libraries(media-capture PkgConfig::GSTREAMER Raven::platform-contracts)
target_include_directories(media-capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
ament_target_dependencies(
  media-capture
  SYSTEM
  rclcpp
  rcl_interfaces
  image_transport
  sensor_msgs
  rclcpp_action
  raven_interfaces
  diagnostic_msgs)
install(TARGETS media-capture RUNTIME DESTINATION lib/${PROJECT_NAME})

ament_package()
