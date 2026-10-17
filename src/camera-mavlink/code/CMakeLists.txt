cmake_minimum_required(VERSION 3.20)
project(
  camera_mavlink
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(raven_interfaces REQUIRED)
find_package(mavlink REQUIRED)
add_executable(camera-mavlink main.cpp node.cpp telemetry.cpp link.cpp)
target_include_directories(camera-mavlink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(camera-mavlink SYSTEM PRIVATE ${mavlink_INCLUDE_DIRS})
ament_target_dependencies(camera-mavlink SYSTEM rclcpp rclcpp_action raven_interfaces)
install(TARGETS camera-mavlink RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
