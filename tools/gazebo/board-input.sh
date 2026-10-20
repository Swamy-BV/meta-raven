#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Select the simulation input, or restore the default camera; no persistent board configuration is changed.
# Usage: ./board-input.sh sim|camera <board-ipv4>
set -euo pipefail
mode=${1:?Choose sim or camera}
board=${2:?Provide the board IPv4 address}
case "$mode" in sim|camera) ;; *) echo 'Choose sim or camera' >&2; exit 2;; esac
if [[ ! "$board" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then echo 'Expected board IPv4 address' >&2; exit 2; fi
ssh -o BatchMode=yes -o ConnectTimeout=12 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 -o IPQoS=none root@"$board" /bin/sh -s -- "$mode" <<'REMOTE'
set -e
mode=$1
override=/run/systemd/system/media-capture.service.d/90-gazebo.conf
if [ "$mode" = sim ]; then
  mkdir -p /run/systemd/system/media-capture.service.d
  cat > "$override" <<'CONF'
[Service]
ExecStart=
ExecStart=/opt/ros/jazzy/lib/media_capture/media-capture --ros-args --params-file /etc/media-capture.yaml -p source:=rtp -p input_uri:=udp://0.0.0.0:5602 -p source_width:=640 -p source_height:=360 -p photo_width:=640 -p photo_height:=360 -p optical_frame:=sim_camera_optical_frame
CONF
else
  rm -f "$override"
fi
systemctl daemon-reload
systemctl restart media-capture
systemctl is-active media-capture vision-engine target-tracker
REMOTE