cmake_minimum_required(VERSION 3.20)
project(
  guidance
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(ruckig 0.9.2 EXACT REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(raven_interfaces REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(px4_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(std_srvs REQUIRED)
add_executable(
  guidance
  main.cpp
  behaviors/follow.cpp
  behaviors/trail.cpp
  behaviors/go_to.cpp
  planning/trajectory.cpp
  validation/motion.cpp
  estimation/motion.cpp
  estimation/pose.cpp
  execution/controller.cpp
  adapters/ros/node.cpp
  adapters/ros/go_to.cpp
  adapters/ros/execution.cpp
  adapters/ros/configuration.cpp
  adapters/ros/observations.cpp
  adapters/ros/telemetry.cpp
  adapters/ros/following.cpp
  adapters/ros/health.cpp)
target_include_directories(guidance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
ament_target_dependencies(
  guidance
  SYSTEM
  rclcpp
  rclcpp_action
  geometry_msgs
  rcl_interfaces
  raven_interfaces
  vision_msgs
  sensor_msgs
  px4_msgs
  diagnostic_msgs
  std_srvs)
get_target_property(RUCKIG_INCLUDE_DIRS ruckig::ruckig INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(guidance SYSTEM PRIVATE ${RUCKIG_INCLUDE_DIRS})
target_link_libraries(guidance ruckig::ruckig)
install(TARGETS guidance RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
