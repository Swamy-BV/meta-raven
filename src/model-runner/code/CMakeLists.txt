cmake_minimum_required(VERSION 3.20)
project(
  model_runner
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(PlatformContracts REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_path(LOCAL_IPC_INCLUDE_DIR local_ipc.h REQUIRED)
find_path(DETECTION_INCLUDE_DIR detection.h REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-unix-2.0)
pkg_check_modules(PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
pkg_check_modules(ONNX REQUIRED IMPORTED_TARGET libonnxruntime)
find_path(TFLITE_INCLUDE_DIR tensorflow/lite/c/c_api.h REQUIRED)
find_library(TFLITE_LIBRARY NAMES tensorflow-lite REQUIRED)
add_executable(model-runner model_runner.cpp inference_worker.cpp onnx_model.cpp neutron_backend.cpp
                            adapters/ros/frame_input.cpp)
target_include_directories(model-runner PRIVATE ${TFLITE_INCLUDE_DIR} ${LOCAL_IPC_INCLUDE_DIR} ${DETECTION_INCLUDE_DIR})
target_link_libraries(
  model-runner
  Microsoft.GSL::GSL
  nlohmann_json::nlohmann_json
  PkgConfig::GLIB
  PkgConfig::PIXBUF
  PkgConfig::ONNX
  Threads::Threads
  Raven::platform-contracts
  ${TFLITE_LIBRARY}
  ${CMAKE_DL_LIBS})
ament_target_dependencies(model-runner SYSTEM rclcpp sensor_msgs)
install(TARGETS model-runner RUNTIME DESTINATION lib/${PROJECT_NAME})

ament_package()
