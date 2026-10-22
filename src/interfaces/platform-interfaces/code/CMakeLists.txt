cmake_minimum_required(VERSION 3.20)
project(raven_interfaces VERSION 1.0.0)
find_package(ament_cmake REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(action_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)
rosidl_generate_interfaces(
  ${PROJECT_NAME}
  "msg/MediaProfile.msg"
  "msg/MediaState.msg"
  "msg/MediaCapabilities.msg"
  "msg/MediaCaptureEvent.msg"
  "srv/SetMediaProfile.srv"
  "srv/SetMediaSource.srv"
  "srv/SetMediaMode.srv"
  "srv/SetMediaOutput.srv"
  "srv/SetCameraControls.srv"
  "srv/SelectTarget.srv"
  "action/CapturePhotos.action"
  "action/GoTo.action"
  "action/Orbit.action"
  DEPENDENCIES
  std_msgs
  geometry_msgs
  action_msgs)
ament_export_dependencies(rosidl_default_runtime)
ament_package()
