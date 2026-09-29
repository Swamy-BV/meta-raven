#!/usr/bin/env bash
set -euo pipefail
project=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$project/release.env"
workspace=${1:?Usage: sync.sh /absolute/path/to/workspace}
mkdir -p "$workspace"
cd "$workspace"
repo init -u "$MANIFEST_URL" -b "$MANIFEST_REVISION" -m "$MANIFEST_FILE"
repo sync -c -j8 --no-tags --fail-fast
