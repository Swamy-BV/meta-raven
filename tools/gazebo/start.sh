#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Run a looping Gazebo camera scene and stream H.264 RTP to a board.
# Usage: ./start.sh courtyard|warehouse <board-ipv4>
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
scene=${1:?Choose courtyard or warehouse}
board=${2:?Provide the board IPv4 address}
case "$scene" in courtyard|warehouse) ;; *) echo 'Unknown scene' >&2; exit 2;; esac
if [[ ! "$board" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo 'Expected board IPv4 address' >&2
  exit 2
fi
mkdir -p "$root/build"
exec 9>"$root/build/stream.lock"
flock -n 9 || { echo 'Run ./stop.sh before changing scenes.' >&2; exit 1; }
cmake -S "$root" -B "$root/build"
cmake --build "$root/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
export GZ_SIM_SYSTEM_PLUGIN_PATH="$root/build${GZ_SIM_SYSTEM_PLUGIN_PATH:+:$GZ_SIM_SYSTEM_PLUGIN_PATH}"
export GZ_PARTITION=raven-camera-simulation
sed "s|<udpHost>[^<]*</udpHost>|<udpHost>$board</udpHost>|" "$root/worlds/$scene.sdf" > "$root/build/active.sdf"
echo "Gazebo $scene -> H.264 RTP $board:5602 (WSL x264; board hardware decoding)"
exec gz sim -s -r -v 3 "$root/build/active.sdf"
