# Components

## Vision and flight

```mermaid
flowchart TD
    source["Camera / files / network video"]
    capture["media-capture<br/>Capture and recording"]
    vision["vision-engine<br/>NPU inference"]
    tracker["target-tracker<br/>Identity and association"]
    guidance["guidance<br/>Following and trajectories"]
    dds["Micro XRCE-DDS Agent<br/>Linux to M7 bridge"]
    px4["M7 PX4 / NuttX<br/>State estimation and flight control"]

    source --> capture
    capture -->|ROS images| vision
    vision -->|Detections| tracker
    tracker -->|Tracked observations| guidance
    guidance <-->|Setpoints and aircraft state| dds
    dds <-->|RPMsg| px4
```

## Camera and external interfaces

```mermaid
flowchart TD
    ground["Ground station"]
    router["mavlink-router"]
    px4["M7 PX4"]
    camera["camera-mavlink"]
    capture["media-capture"]
    ros["ROS applications"]
    preview["MediaMTX"]
    browser["Browser"]

    ground <-->|MAVLink| router
    router <-->|RPMsg MAVLink| px4
    router <-->|Camera MAVLink| camera
    camera <-->|ROS controls and events| capture
    ros <-->|Images, CameraInfo and capture operations| capture
    capture -->|H.264 RTP| preview
    preview -->|WebRTC| browser
```

## Current hardware-in-the-loop evaluation

```mermaid
flowchart TD
    airsim["Windows AirSim<br/>Scene and aircraft dynamics"]
    bridge["Host HITL bridge"]
    px4["Actual i.MX95 M7 PX4"]
    onboard["i.MX95 Linux<br/>Capture, vision, tracking and guidance"]

    airsim <-->|Simulated sensors and actuator outputs| bridge
    bridge <-->|Ethernet HITL MAVLink| px4
    airsim -->|Network video| onboard
    onboard <-->|RPMsg DDS via Agent| px4
```

## Visual odometry evaluation

`visual-odometry` runs the upstream OpenVINS ROS 2 node on i.MX95 Linux.
`px4-odometry` checks acquisition time, frames and covariance, then converts
the estimate into PX4 FRD measurements over the existing DDS link. The adapter
is disabled by default. GPS-aiding-disabled M7 hover and landing have passed
with horizontal vision fusion; barometer and magnetometer remain active.
VIO height drift and more motion cases still need qualification.

```mermaid
flowchart TD
    sim["Windows AirSim"]
    bridge["Upstream C++ ROS 2 bridge"]
    camera["Fixed navigation camera<br/>sensor_msgs/Image"]
    imu["Navigation IMU<br/>sensor_msgs/Imu"]
    vio["visual-odometry<br/>OpenVINS on i.MX95"]
    output["/localization/vio/odometry<br/>nav_msgs/Odometry"]
    evaluation["Independent drift evaluation"]
    adapter["px4-odometry<br/>Validate and convert"]
    dds["Micro XRCE-DDS Agent"]
    px4["M7 PX4 EKF2"]

    sim --> bridge
    bridge --> camera
    bridge --> imu
    camera --> vio
    imu --> vio
    vio --> output
    output --> evaluation
    sim -.->|Ground truth only| evaluation
    output --> adapter
    adapter -->|VehicleOdometry| dds
    dds <-->|RPMsg| px4
```
