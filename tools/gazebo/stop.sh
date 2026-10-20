#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Stop this simulator and wait until its process releases the scene lock.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export GZ_PARTITION=raven-camera-simulation
gz service -s /server_control --reqtype gz.msgs.ServerControl --reptype gz.msgs.Boolean --timeout 5000 --req 'stop: true'
flock --wait 10 "$root/build/stream.lock" true
