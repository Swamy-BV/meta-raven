cmake_minimum_required(VERSION 3.20)
project(
  target_follower
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(raven_interfaces REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(px4_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(std_srvs REQUIRED)
add_executable(target-follower main.cpp core/follower.cpp adapters/ros/node.cpp)
target_include_directories(target-follower PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
ament_target_dependencies(
  target-follower
  SYSTEM
  rclcpp
  rcl_interfaces
  raven_interfaces
  vision_msgs
  sensor_msgs
  px4_msgs
  diagnostic_msgs
  std_srvs)
install(TARGETS target-follower RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
