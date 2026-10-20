cmake_minimum_required(VERSION 3.20)
project(
  raven_ros_config
  VERSION 1.0.0
  LANGUAGES NONE)
find_package(ament_cmake REQUIRED)
set(ROS_CONFIG_DIR
    "/etc/ros"
    CACHE PATH "System ROS middleware configuration")
ament_environment_hooks("${CMAKE_CURRENT_SOURCE_DIR}/middleware.dsv.in")
configure_file(ros2.env.in ros2.env @ONLY)
install(FILES cyclonedds.xml "${CMAKE_CURRENT_BINARY_DIR}/ros2.env" DESTINATION "${ROS_CONFIG_DIR}")
ament_package()
