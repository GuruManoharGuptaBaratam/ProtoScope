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
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -g \
    -I "$proto_root/components/proto_rs485/include" \
    "$proto_root/tests/host/test_rs485_timing.c" \
    "$proto_root/components/proto_rs485/proto_rs485_timing.c" \
    -o "$proto_test_dir/test_rs485_timing"
"$proto_test_dir/test_rs485_timing"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -g \
    -I "$proto_root/components/proto_i2c/include" \
    -I "$proto_root/components/proto_spi/include" \
    "$proto_root/tests/host/test_i2c_spi_codec.c" \
    "$proto_root/components/proto_i2c/proto_i2c_codec.c" \
    "$proto_root/components/proto_spi/proto_spi_codec.c" \
    -o "$proto_test_dir/test_i2c_spi_codec"
"$proto_test_dir/test_i2c_spi_codec"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -g \
    -I "$proto_root/components/proto_event/include" \
    -I "$proto_root/components/proto_twai/include" \
    -I "$proto_root/components/proto_uart/include" \
    -I "$proto_root/components/proto_i2c/include" \
    -I "$proto_root/components/proto_spi/include" \
    "$proto_root/tests/host/test_proto_event.c" \
    "$proto_root/components/proto_event/proto_event.c" \
    "$proto_root/components/proto_event/proto_event_caps.c" \
    "$proto_root/components/proto_event/proto_hex.c" \
    "$proto_root/components/proto_twai/proto_can_codec.c" \
    "$proto_root/components/proto_uart/proto_uart_framer.c" \
    "$proto_root/components/proto_i2c/proto_i2c_codec.c" \
    "$proto_root/components/proto_spi/proto_spi_codec.c" \
    -o "$proto_test_dir/test_proto_event"
"$proto_test_dir/test_proto_event"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -g \
    -I "$proto_root/components/proto_route/include" \
    -I "$proto_root/components/proto_event/include" \
    -I "$proto_root/components/proto_twai/include" \
    -I "$proto_root/components/proto_uart/include" \
    -I "$proto_root/components/proto_i2c/include" \
    -I "$proto_root/components/proto_spi/include" \
    "$proto_root/tests/host/test_proto_route.c" \
    "$proto_root/components/proto_route/proto_route.c" \
    "$proto_root/components/proto_route/proto_route_engine.c" \
    "$proto_root/components/proto_event/proto_event.c" \
    "$proto_root/components/proto_event/proto_event_caps.c" \
    "$proto_root/components/proto_event/proto_hex.c" \
    "$proto_root/components/proto_twai/proto_can_codec.c" \
    "$proto_root/components/proto_uart/proto_uart_framer.c" \
    "$proto_root/components/proto_i2c/proto_i2c_codec.c" \
    "$proto_root/components/proto_spi/proto_spi_codec.c" \
    -o "$proto_test_dir/test_proto_route"
"$proto_test_dir/test_proto_route"
python3 "$proto_root/tests/host/test_n16r8_config.py"
