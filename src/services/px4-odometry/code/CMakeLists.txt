cmake_minimum_required(VERSION 3.20)
project(
  px4_odometry
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(px4_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
add_executable(px4-odometry main.cpp node.cpp conversion.cpp)
ament_target_dependencies(
  px4-odometry
  SYSTEM
  rclcpp
  nav_msgs
  px4_msgs
  diagnostic_msgs
  geometry_msgs)
target_link_libraries(px4-odometry Eigen3::Eigen)
get_target_property(EIGEN_INCLUDE_DIRS Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(px4-odometry SYSTEM PRIVATE ${EIGEN_INCLUDE_DIRS})
install(TARGETS px4-odometry RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
