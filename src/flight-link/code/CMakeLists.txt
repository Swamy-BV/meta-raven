cmake_minimum_required(VERSION 3.20)
project(
  flight_link
  VERSION 0.1.0
  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(flight-core STATIC core/vehicle_state.cpp)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(raven_interfaces REQUIRED)
find_package(MAVSDK REQUIRED)
add_executable(flight-link main.cpp adapters/ros/node.cpp adapters/mavsdk/backend.cpp)
target_link_libraries(flight-link flight-core MAVSDK::mavsdk)
ament_target_dependencies(flight-link SYSTEM rclcpp diagnostic_msgs raven_interfaces)
install(TARGETS flight-link DESTINATION lib/${PROJECT_NAME})
ament_package()
