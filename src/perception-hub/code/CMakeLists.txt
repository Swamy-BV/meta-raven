cmake_minimum_required(VERSION 3.20)
project(
  perception_hub
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(nlohmann_json REQUIRED)
add_library(perception-decode STATIC adapters/runner/decode.cpp)
target_link_libraries(perception-decode PUBLIC nlohmann_json::nlohmann_json)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(raven_interfaces REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
find_path(LOCAL_IPC_INCLUDE_DIR local_ipc.h REQUIRED)
add_executable(perception-hub main.cpp adapters/ros/node.cpp adapters/runner/client.cpp)
target_include_directories(perception-hub SYSTEM PRIVATE ${LOCAL_IPC_INCLUDE_DIR})
target_link_libraries(perception-hub perception-decode PkgConfig::GLIB)
ament_target_dependencies(
  perception-hub
  SYSTEM
  rclcpp
  rcl_interfaces
  diagnostic_msgs
  vision_msgs
  std_msgs
  raven_interfaces)
install(TARGETS perception-hub DESTINATION lib/${PROJECT_NAME})
ament_package()
