#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Select the simulation input, or restore the default camera; no persistent board configuration is changed.
# Usage: ./board-input.sh sim|flight|camera <board-ipv4>
set -euo pipefail
mode=${1:?Choose sim, flight or camera}
board=${2:?Provide the board IPv4 address}
case "$mode" in sim|flight|camera) ;; *) echo 'Choose sim, flight or camera' >&2; exit 2;; esac
if [[ ! "$board" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then echo 'Expected board IPv4 address' >&2; exit 2; fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [ "$mode" = flight ]; then
  ssh -o BatchMode=yes -o ConnectTimeout=12 root@"$board" 'mkdir -p /run/raven-simulation; cat > /run/raven-simulation/camera.yaml' < "$root/config/front-camera.yaml"
fi
ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 -o IPQoS=none root@"$board" /bin/sh -s -- "$mode" <<'REMOTE'
set -e
mode=$1
override=/run/systemd/system/media-capture.service.d/90-gazebo.conf
if [ "$mode" != camera ]; then
  mkdir -p /run/systemd/system/media-capture.service.d
  cat > "$override" <<'CONF'
[Service]
ExecStart=
ExecStart=/opt/ros/jazzy/lib/media_capture/media-capture --ros-args --params-file /etc/media-capture.yaml -p source:=rtp -p input_uri:=udp://0.0.0.0:5602 -p source_width:=640 -p source_height:=360 -p photo_width:=640 -p photo_height:=360 -p optical_frame:=sim_camera_optical_frame
CONF
  if [ "$mode" = flight ]; then
    sed -i '/^ExecStart=.*media-capture /s|$| -p camera_info_url:=file:///run/raven-simulation/camera.yaml -p use_sim_time:=true|' "$override"
    cat > /run/systemd/system/px4-simulation-agent.service <<'UNIT'
[Unit]
Description=Temporary DDS agent for isolated PX4 SITL
After=network.target
[Service]
Type=simple
User=px4-dds
Group=px4-dds
Environment=LD_LIBRARY_PATH=/opt/ros/jazzy/lib
Environment=FASTDDS_BUILTIN_TRANSPORTS=UDPv4
ExecStart=/opt/ros/jazzy/bin/MicroXRCEAgent udp4 -p 8889 -v 3
Restart=on-failure
RestartSec=5
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
UNIT
  fi
else
  rm -f "$override"
fi
# Apply the same ROS clock to the complete observation/control path. All
# overrides live in /run and vanish on reboot. Physical operation uses wall time.
for service in vision-engine target-tracker guidance; do
  path=/run/systemd/system/$service.service.d/90-gazebo.conf
  if [ "$mode" = flight ]; then
    mkdir -p "${path%/*}"
    package=$(printf '%s' "$service" | tr '-' '_')
    cat > "$path" <<CONF
[Service]
ExecStart=
ExecStart=/opt/ros/jazzy/lib/$package/$service --ros-args --params-file /etc/$service.yaml -p use_sim_time:=true
CONF
  else
    rm -f "$path"
  fi
done
systemctl daemon-reload
systemctl stop guidance
systemctl restart media-capture vision-engine target-tracker
if [ "$mode" = flight ]; then
  systemctl start px4-simulation-agent.service
fi
systemctl is-active media-capture vision-engine target-tracker
REMOTE