#!/usr/bin/env bash
set -eo pipefail
project=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
workspace=${1:?Usage: build.sh /absolute/path/to/workspace [bitbake arguments]}
shift
if [[ ${ACCEPT_FSL_EULA:-} != 1 ]]; then
    echo "Read sources/meta-imx/LICENSE.txt and explicitly set ACCEPT_FSL_EULA=1 to build."
    exit 1
fi
source "$project/scripts/setup.sh" "$workspace"
set -u
if (( $# )); then
    bitbake "$@"
else
    bitbake "$IMAGE"
fi
