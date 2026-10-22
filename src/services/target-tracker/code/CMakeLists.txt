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
find_package(std_srvs REQUIRED)
find_package(ByteTrack 1.0 CONFIG REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc video calib3d)
add_executable(target-tracker main.cpp core/tracker.cpp adapters/ros/node.cpp adapters/ros/images.cpp
                              adapters/opencv/camera_motion.cpp)
target_include_directories(target-tracker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(target-tracker SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(target-tracker ByteTrack::ByteTrack ${OpenCV_LIBS})
ament_target_dependencies(
  target-tracker
  SYSTEM
  rclcpp
  rcl_interfaces
  vision_msgs
  diagnostic_msgs
  std_msgs
  std_srvs
  sensor_msgs)
install(TARGETS target-tracker RUNTIME DESTINATION lib/${PROJECT_NAME})
include(CTest)
if(BUILD_TESTING)
  add_executable(camera-motion-test tests/camera_motion.cpp adapters/opencv/camera_motion.cpp)
  target_include_directories(camera-motion-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(camera-motion-test SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(camera-motion-test PRIVATE ${OpenCV_LIBS})
  add_test(NAME camera-motion-estimation COMMAND camera-motion-test)
endif()
ament_package()
