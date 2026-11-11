#!/usr/bin/env bash
set -euo pipefail

phase1_tools_dir=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# sdkconfig.defaults selects ESP32-S3 on a clean build. Reconfigure preserves
# reviewed board settings; set-target would reset sdkconfig on every run.
"$phase1_tools_dir/idf_phase1.sh" reconfigure build

echo "Build verification passed."
echo "For hardware verification run: ./tools/idf_phase1.sh -p PORT flash monitor"
echo "Then run the console commands: help, status, heap"
