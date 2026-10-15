```mermaid
flowchart LR
    source[Camera / network video / replay] --> media
    media -->|RTP video| receiver[Video receiver]
    media -->|ROS 2 Image / CameraInfo| ros

    subgraph linux[Linux]
        media[media-capture]
        runner[model-runner]
        perception[perception-hub]
        flight[flight-link]
        media -->|ROS 2 Image| runner
        perception <-->|Local inference requests / results| runner
    end

    perception -->|ROS 2 vision / metadata / health| ros[ROS consumers]
    flight -->|ROS 2 state / health| ros
    flight <-->|MAVSDK / MAVLink over RPMsg| px4[PX4 on M7]
```
