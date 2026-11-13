#include "proto_hex.h"

#include <string.h>

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

bool proto_hex_parse(const char *hex, uint8_t *out, size_t max, size_t *length)
{
    if (!hex || !out || !length) { return false; }
    const size_t n = strlen(hex);
    if (n == 0 || (n % 2) != 0 || n / 2 > max) { return false; }
    for (size_t i = 0; i < n; i += 2) {
        const int hi = hex_value(hex[i]);
        const int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) { return false; }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    *length = n / 2;
    return true;
}

size_t proto_hex_format(const uint8_t *data, size_t length, char *out, size_t out_size)
{
    static const char digits[] = "0123456789ABCDEF";
    if (!out || out_size == 0) { return 0; }
    if ((!data && length) || out_size < 2 * length + 1) { out[0] = '\0'; return 0; }
    for (size_t i = 0; i < length; ++i) {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 0xF];
    }
    out[2 * length] = '\0';
    return 2 * length;
}
