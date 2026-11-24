#!/usr/bin/env bash
set -euo pipefail

proto_tools_dir=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
"$proto_tools_dir/test_host.sh"
"$proto_tools_dir/idf_phase1.sh" reconfigure build
echo "Phase 9 software verification passed; SD card, display and encoder hardware tests remain required."
