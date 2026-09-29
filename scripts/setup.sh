#!/usr/bin/env bash
# Execute to configure, or source to also enter the configured environment.
raven_project=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$raven_project/release.env"
raven_workspace=${1:?Usage: setup.sh /absolute/path/to/workspace}
raven_machine=${RAVEN_MACHINE:-$MACHINE}
raven_distro=$DISTRO
cd "$raven_workspace" || return 1 2>/dev/null || exit 1
# A value of 0 explicitly leaves the license unaccepted during setup.
# NXP's script restores its baseline configuration before Raven settings.
EULA=${ACCEPT_FSL_EULA:-0} MACHINE=imx95-15x15-lpddr4x-frdm SDKMACHINE=x86_64 DISTRO="$raven_distro" source ./imx-setup-release.sh -b build-raven
raven_setup_status=$?
if (( raven_setup_status != 0 )); then
    return "$raven_setup_status" 2>/dev/null || exit "$raven_setup_status"
fi
cat >> conf/bblayers.conf <<'EOF'
BBLAYERS += "${BSPDIR}/sources/meta-ros/meta-ros-common"
BBLAYERS += "${BSPDIR}/sources/meta-ros/meta-ros2"
BBLAYERS += "${BSPDIR}/sources/meta-ros/meta-ros2-jazzy"
BBLAYERS += "${BSPDIR}/sources/meta-raven"
EOF
cat >> conf/local.conf <<EOF
MACHINE = "$raven_machine"
ACCEPT_FSL_EULA = "${ACCEPT_FSL_EULA:-0}"
BB_NUMBER_THREADS = "8"
PARALLEL_MAKE = "-j 8"
EOF
"${REPO:-$HOME/bin/repo}" manifest -r -o "$PWD/resolved-manifest.xml"
echo "Raven setup complete: $raven_machine. No compilation started."
