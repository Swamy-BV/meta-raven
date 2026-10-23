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

OpenVINS runs in shadow mode on i.MX95 Linux. The upstream AirSim C++ ROS 2
bridge supplies the navigation camera and IMU; a separate evaluator receives
ground truth. AirSim's internal pilot flies these tests. PX4 fusion remains
an integration step after initialization, drift and timing are qualified.

```mermaid
flowchart TD
    sim["Windows AirSim"]
    bridge["Upstream C++ ROS 2 bridge"]
    camera["Fixed navigation camera<br/>Image + CameraInfo"]
    imu["Navigation IMU<br/>Imu"]
    vio["visual-odometry<br/>OpenVINS on i.MX95"]
    output["/localization/vio/odometry<br/>nav_msgs/Odometry"]
    evaluation["Independent drift evaluation"]
    adapter["PX4 odometry adapter<br/>Planned"]
    px4["M7 PX4 EKF2"]

    sim --> bridge
    bridge --> camera
    bridge --> imu
    camera --> vio
    imu --> vio
    vio --> output
    output --> evaluation
    sim -.->|Ground truth only| evaluation
    output -.-> adapter
    adapter -.->|Existing DDS link| px4
```
