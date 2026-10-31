cmake_minimum_required(VERSION 3.20)
project(
  navigation_frames
  VERSION 1.0.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Eigen3 REQUIRED)
add_library(navigation-geometry STATIC core/alignment.cpp)
target_include_directories(navigation-geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(navigation-geometry PUBLIC Eigen3::Eigen)
get_target_property(EIGEN_INCLUDE_DIRS Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(navigation-geometry SYSTEM PUBLIC ${EIGEN_INCLUDE_DIRS})

option(NAVIGATION_FRAMES_ROS "Build the ROS 2 frame-authority adapter" ON)
if(NAVIGATION_FRAMES_ROS)
  find_package(ament_cmake REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(rcl_interfaces REQUIRED)
  find_package(nav_msgs REQUIRED)
  find_package(geometry_msgs REQUIRED)
  find_package(diagnostic_msgs REQUIRED)
  find_package(std_srvs REQUIRED)
  find_package(px4_msgs REQUIRED)
  find_package(tf2_ros REQUIRED)
  add_executable(navigation-frames main.cpp adapters/ros/node.cpp adapters/ros/configuration.cpp
                                   adapters/ros/measurements.cpp)
  target_link_libraries(navigation-frames navigation-geometry)
  ament_target_dependencies(
    navigation-frames
    SYSTEM
    rclcpp
    rcl_interfaces
    nav_msgs
    geometry_msgs
    diagnostic_msgs
    std_srvs
    px4_msgs
    tf2_ros)
  install(TARGETS navigation-frames RUNTIME DESTINATION lib/${PROJECT_NAME})
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(navigation-geometry-test tests/alignment_test.cpp)
  target_link_libraries(navigation-geometry-test PRIVATE navigation-geometry)
  foreach(
    case IN
    ITEMS interpolation
          bounds
          invalid
          freshness
          frame-conversion
          alignment
          reset
          drift
          rotation)
    add_test(NAME navigation-${case} COMMAND navigation-geometry-test ${case})
  endforeach()
endif()
if(NAVIGATION_FRAMES_ROS)
  ament_package()
endif()
