#!/usr/bin/env bash
set -euo pipefail

proto_root=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
proto_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/protoscope-host.XXXXXX")
trap 'rm -rf -- "$proto_test_dir"' EXIT

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -g \
    -I "$proto_root/components/proto_uart/include" \
    "$proto_root/tests/host/test_uart_framer.c" \
    "$proto_root/components/proto_uart/proto_uart_framer.c" \
    -o "$proto_test_dir/test_uart_framer"
"$proto_test_dir/test_uart_framer"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -g \
    -I "$proto_root/components/proto_twai/include" \
    "$proto_root/tests/host/test_can_codec.c" \
    "$proto_root/components/proto_twai/proto_can_codec.c" \
    -o "$proto_test_dir/test_can_codec"
"$proto_test_dir/test_can_codec"
python3 "$proto_root/tests/host/test_n16r8_config.py"
