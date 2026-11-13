#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parse an even-length hex string (no separators) into out. Returns false for
 * odd length, a bad digit, empty input or more than max bytes. */
bool proto_hex_parse(const char *hex, uint8_t *out, size_t max, size_t *length);
/* Upper-case hex of data into out (needs 2*length+1). Returns characters written. */
size_t proto_hex_format(const uint8_t *data, size_t length, char *out, size_t out_size);
