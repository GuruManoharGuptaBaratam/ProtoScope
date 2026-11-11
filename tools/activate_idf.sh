# Source this file; do not execute it:   source tools/activate_idf.sh
# Activates ESP-IDF v5.5.6 for ESP32-S3 in the current shell (bash or zsh).
# Used by tools/idf_phase1.sh and by the VS Code "ProtoScope IDF" terminal profile.

_proto_activate_idf() {
    local idf_dir="${PROTOSCOPE_IDF_DIR:-$HOME/esp/esp-idf-v5.5.6}"
    local tools_dir="${PROTOSCOPE_IDF_TOOLS_PATH:-$HOME/.espressif}"
    local python_env="" candidate

    if [ ! -f "$idf_dir/export.sh" ]; then
        echo "ESP-IDF v5.5.6 is missing at: $idf_dir" >&2
        echo "Install: git clone -b v5.5.6 --recursive https://github.com/espressif/esp-idf.git $idf_dir && cd $idf_dir && ./install.sh esp32s3" >&2
        return 1
    fi

    # Pick the 5.5 Python environment explicitly so a previously activated
    # 5.4 / 6.x environment is never reused.
    for candidate in $(ls -d "$tools_dir"/python_env/idf5.5_py*_env 2>/dev/null); do
        if [ -x "$candidate/bin/python" ]; then
            python_env="$candidate"
            break
        fi
    done
    if [ -z "$python_env" ]; then
        echo "The ESP-IDF 5.5 Python environment is missing under: $tools_dir" >&2
        echo "Run once: cd \"$idf_dir\" && ./install.sh esp32s3" >&2
        return 1
    fi

    export IDF_TOOLS_PATH="$tools_dir"
    export IDF_PYTHON_ENV_PATH="$python_env"
    export IDF_TARGET=esp32s3
    # shellcheck disable=SC1091
    . "$idf_dir/export.sh" >/dev/null 2>&1

    if [ "${IDF_PATH:-}" != "$idf_dir" ] || [ "$(command -v python)" != "$python_env/bin/python" ]; then
        echo "Could not activate the ESP-IDF v5.5.6 environment (IDF_PATH='${IDF_PATH:-}')." >&2
        return 1
    fi
    if ! "$python_env/bin/python" "$idf_dir/tools/idf.py" --version 2>/dev/null | grep -q "v5\.5"; then
        echo "Activated SDK is not ESP-IDF v5.5.x." >&2
        return 1
    fi
    echo "ProtoScope: $("$python_env/bin/python" "$idf_dir/tools/idf.py" --version 2>/dev/null | head -n1) active, target esp32s3."
    return 0
}

_proto_activate_idf
_proto_activate_status=$?
unset -f _proto_activate_idf
(exit "$_proto_activate_status")
