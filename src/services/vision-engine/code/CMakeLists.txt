cmake_minimum_required(VERSION 3.20)
project(
  vision_engine
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(PlatformContracts REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-unix-2.0)
pkg_check_modules(PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
pkg_check_modules(ONNX REQUIRED IMPORTED_TARGET libonnxruntime)
find_path(TFLITE_INCLUDE_DIR tensorflow/lite/c/c_api.h REQUIRED)
find_library(TFLITE_LIBRARY NAMES tensorflow-lite REQUIRED)
add_executable(
  vision-engine
  main.cpp
  application/engine.cpp
  core/decode.cpp
  inference_worker.cpp
  onnx_model.cpp
  yolov8_model.cpp
  neutron_backend.cpp
  adapters/ros/frame_input.cpp
  adapters/ros/node.cpp)
target_include_directories(vision-engine PRIVATE ${TFLITE_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vision-engine SYSTEM PRIVATE ${GLIB_INCLUDE_DIRS} ${PIXBUF_INCLUDE_DIRS})
target_link_libraries(
  vision-engine
  Microsoft.GSL::GSL
  nlohmann_json::nlohmann_json
  PkgConfig::GLIB
  PkgConfig::PIXBUF
  PkgConfig::ONNX
  Threads::Threads
  Raven::platform-contracts
  ${TFLITE_LIBRARY}
  ${CMAKE_DL_LIBS})
ament_target_dependencies(
  vision-engine
  SYSTEM
  rclcpp
  rcl_interfaces
  sensor_msgs
  std_msgs
  std_srvs
  diagnostic_msgs
  vision_msgs)
install(TARGETS vision-engine RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
