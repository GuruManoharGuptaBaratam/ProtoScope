#!/usr/bin/env bash
# ProtoScope ESP-IDF launcher: activates ESP-IDF v5.5.6 and runs idf.py.
# ESP-IDF cannot build in a path containing spaces (for example
# ".../Desktop/My Projects/ProtoScope"). In that case this script keeps your
# project where it is, mirrors the source into a space-free build folder
# (default: ~/esp/protoscope-build) and builds/flashes from there.
set -eo pipefail

proto_tools_dir=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
proto_project_dir=$(cd -P -- "$proto_tools_dir/.." && pwd)

# shellcheck source=activate_idf.sh
source "$proto_tools_dir/activate_idf.sh"

proto_build_dir="$proto_project_dir"
case "$proto_project_dir" in
    *" "*)
        proto_build_dir="${PROTOSCOPE_BUILD_MIRROR:-$HOME/esp/protoscope-build}"
        case "$proto_build_dir" in
            *" "*) echo "PROTOSCOPE_BUILD_MIRROR must not contain spaces: $proto_build_dir" >&2; exit 2 ;;
        esac
        mkdir -p "$proto_build_dir"
        echo "Project path has spaces; building from mirror: $proto_build_dir"
        if command -v rsync >/dev/null 2>&1; then
            rsync -a --delete \
                --exclude '/build' --exclude '/sdkconfig' --exclude '/sdkconfig.old' \
                --exclude '/sdkconfig.before-n16r8-*' --exclude '/dependencies.lock' \
                --exclude '/managed_components' --exclude '/logs' \
                --exclude '.git' --exclude '.vscode' --exclude '__pycache__' --exclude '*.pyc' \
                "$proto_project_dir/" "$proto_build_dir/"
        else
            # Fallback without rsync: replace the source folders, keep build/sdkconfig.
            rm -rf "$proto_build_dir/components" "$proto_build_dir/main" "$proto_build_dir/tools" \
                   "$proto_build_dir/tests" "$proto_build_dir/docs"
            tar -C "$proto_project_dir" \
                --exclude='./build' --exclude='./sdkconfig' --exclude='./sdkconfig.old' --exclude='./sdkconfig.before-n16r8-*' --exclude='./dependencies.lock' \
                --exclude='./managed_components' --exclude='./logs' --exclude='.git' \
                --exclude='.vscode' --exclude='__pycache__' -cf - . | tar -C "$proto_build_dir" -xf -
        fi
        ;;
esac

# Fail early (instead of a confusing link error such as "undefined reference to
# app_main") if the build folder is missing or has empty source files.
for proto_required in CMakeLists.txt sdkconfig.defaults main/CMakeLists.txt main/app_main.cpp \
                      components/proto_twai/proto_can.c components/proto_uart/proto_uart.c; do
    if [[ ! -s "$proto_build_dir/$proto_required" ]]; then
        echo "ERROR: '$proto_required' is missing or empty in $proto_build_dir" >&2
        echo "Source folder: $proto_project_dir -> check that file there (iCloud 'Optimize Mac Storage' can leave empty placeholders)." >&2
        echo "Then remove the build folder and retry:  rm -rf \"$proto_build_dir\"" >&2
        exit 3
    fi
done
if ! grep -q 'app_main' "$proto_build_dir/main/app_main.cpp"; then
    echo "ERROR: main/app_main.cpp in $proto_build_dir does not define app_main." >&2
    exit 3
fi

cd "$proto_build_dir"
# Idempotent: only changes an existing sdkconfig that still has old memory settings.
python3 "$proto_build_dir/tools/configure_n16r8.py" >/dev/null
if [[ $# -eq 0 ]]; then
    set -- build
fi
exec "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"
