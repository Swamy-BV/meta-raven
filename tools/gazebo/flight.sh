#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Run firmware-matched PX4 SITL against an already running street-follow scene.
# Usage: ./flight.sh start|stop <PX4-source> [board-ipv4]
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mode=${1:?Choose start or stop}
px4=$(realpath -- "${2:?Provide the firmware-matched PX4 checkout}")
case "$mode" in
  stop) exec "$px4/build/px4_sitl_default/bin/px4-shutdown" --instance 1;;
  start) ;;
  *) echo 'Choose start or stop' >&2; exit 2;;
esac
expected=$(sed -n 's/^RAVEN_PX4_SRCREV = "\(.*\)"/\1/p' "$root/../../conf/distro/include/raven-px4.inc")
if [ "$(git -C "$px4" rev-parse HEAD)" != "$expected" ]; then
  echo 'PX4 checkout must match the firmware pin used to build px4_msgs.' >&2
  exit 1
fi
board=${3:?Provide the board IPv4 address}
if [[ ! "$board" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then echo 'Expected board IPv4 address' >&2; exit 2; fi
read -r first second third fourth <<< "${board//./ }"
agent_ip=$((first * 16777216 + second * 65536 + third * 256 + fourth))
mkdir -p "$root/build/flight"
exec 9>"$root/build/flight.lock"
flock -n 9 || { echo 'PX4 simulation is already running.' >&2; exit 1; }
export GZ_PARTITION=raven-camera-simulation
export GZ_SIM_RESOURCE_PATH="$root/models:$px4/Tools/simulation/gz/models${GZ_SIM_RESOURCE_PATH:+:$GZ_SIM_RESOURCE_PATH}"
export PX4_SYS_AUTOSTART=4001 PX4_SIMULATOR=gz PX4_GZ_STANDALONE=1 PX4_GZ_WORLD=street_follow PX4_GZ_MODEL_NAME=x500_1
export PX4_UXRCE_DDS_NS=simulation PX4_UXRCE_DDS_PORT=8889
export PX4_PARAM_UXRCE_DDS_AG_IP="$agent_ip"
# Match the board-local ROS domain, and use /clock rather than bridging two
# clocks that can advance at different rates under simulator load.
export PX4_PARAM_UXRCE_DDS_PTCFG=1 PX4_PARAM_UXRCE_DDS_SYNCT=0 PX4_PARAM_UXRCE_DDS_DOM_ID=0
# Instance 1 has system ID 2. The real M7 keeps its existing ID and /fmu topics.
if [ ! -x "$root/build/simulation-clock" ]; then
  echo 'Start the street-follow scene before starting PX4.' >&2
  exit 1
fi
"$root/build/simulation-clock" "$board" &
clock=$!
trap 'kill "$clock" 2>/dev/null || true; wait "$clock" 2>/dev/null || true' EXIT
"$px4/build/px4_sitl_default/bin/px4" -d "$px4/build/px4_sitl_default/etc" -i 1 -w "$root/build/flight"
