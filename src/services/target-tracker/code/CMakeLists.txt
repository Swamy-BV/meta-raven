cmake_minimum_required(VERSION 3.20)
project(
  target_tracker
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(px4_msgs REQUIRED)
find_package(NeutronRuntime 1.0 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
find_library(TFLITE_LIBRARY NAMES tensorflow-lite REQUIRED)
find_package(std_srvs REQUIRED)
find_package(ByteTrack 1.0 CONFIG REQUIRED)
add_executable(
  target-tracker
  main.cpp
  adapters/bytetrack/tracker.cpp
  adapters/bytetrack/appearance.cpp
  adapters/neutron/vehicle_encoder.cpp
  adapters/ros/node.cpp
  adapters/ros/appearance.cpp
  adapters/ros/ground.cpp
  core/ground_motion.cpp
  core/ground_projector.cpp)
target_include_directories(target-tracker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(target-tracker ByteTrack::ByteTrack Neutron::Runtime PkgConfig::PIXBUF ${TFLITE_LIBRARY})
ament_target_dependencies(
  target-tracker
  SYSTEM
  rclcpp
  rcl_interfaces
  vision_msgs
  diagnostic_msgs
  std_msgs
  sensor_msgs
  px4_msgs
  std_srvs)
install(TARGETS target-tracker RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
