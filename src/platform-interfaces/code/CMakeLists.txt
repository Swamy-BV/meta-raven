cmake_minimum_required(VERSION 3.20)
project(raven_interfaces)
find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)
rosidl_generate_interfaces(
  ${PROJECT_NAME}
  "msg/VehicleState.msg"
  "msg/PerceptionResult.msg"
  DEPENDENCIES
  builtin_interfaces
  std_msgs
  vision_msgs)
ament_export_dependencies(rosidl_default_runtime)
ament_package()
