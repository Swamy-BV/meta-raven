#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Run a looping Gazebo camera scene and stream H.264 RTP to a board.
# Usage: ./start.sh courtyard|warehouse|street <board-ipv4>
#        ./start.sh street-follow <board-ipv4> <PX4-source>
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
scene=${1:?Choose courtyard, warehouse, street or street-follow}
board=${2:?Provide the board IPv4 address}
case "$scene" in courtyard|warehouse|street|street-follow) ;; *) echo 'Unknown scene' >&2; exit 2;; esac
if [[ ! "$board" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo 'Expected board IPv4 address' >&2
  exit 2
fi
mkdir -p "$root/build"
exec 9>"$root/build/stream.lock"
flock -n 9 || { echo 'Run ./stop.sh before changing scenes.' >&2; exit 1; }
cmake -S "$root" -B "$root/build" -DPX4_SOURCE_DIR="${3:-}"
cmake --build "$root/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
export GZ_SIM_SYSTEM_PLUGIN_PATH="$root/build${GZ_SIM_SYSTEM_PLUGIN_PATH:+:$GZ_SIM_SYSTEM_PLUGIN_PATH}"
export GZ_PARTITION=raven-camera-simulation
if [ "$scene" = street-follow ]; then
  px4=$(realpath -- "${3:?Provide the firmware-matched PX4 checkout for street-follow}")
  export GZ_SIM_RESOURCE_PATH="$root/models:$px4/Tools/simulation/gz/models${GZ_SIM_RESOURCE_PATH:+:$GZ_SIM_RESOURCE_PATH}"
  export GZ_SIM_SYSTEM_PLUGIN_PATH="$GZ_SIM_SYSTEM_PLUGIN_PATH:$px4/build/px4_sitl_default/src/modules/simulation/gz_plugins"
  xsltproc "$root/worlds/street-follow.xslt" "$root/worlds/street.sdf" > "$root/build/flight-world.sdf"
  world="$root/build/flight-world.sdf"
else
  world="$root/worlds/$scene.sdf"
fi
sed "s|<udpHost>[^<]*</udpHost>|<udpHost>$board</udpHost>|" "$world" > "$root/build/active.sdf"
echo "Gazebo $scene -> H.264 RTP $board:5602 (WSL x264; board hardware decoding)"
exec gz sim -s -r -v 3 "$root/build/active.sdf"
