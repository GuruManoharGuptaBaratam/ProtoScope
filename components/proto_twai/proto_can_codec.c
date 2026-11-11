#include "proto_can_codec.h"

#include <string.h>

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

bool proto_can_bitrate_supported(uint32_t bit_rate)
{
    return bit_rate == 125000u || bit_rate == 250000u ||
           bit_rate == 500000u || bit_rate == 1000000u;
}

bool proto_can_frame_valid(const proto_can_frame_t *frame)
{
    if (!frame || frame->dlc > PROTO_CAN_MAX_DLC) { return false; }
    if (frame->id > (frame->extended ? PROTO_CAN_EXT_ID_MAX : PROTO_CAN_STD_ID_MAX)) {
        return false;
    }
    if (frame->rtr && frame->dlc != 0) { return false; }
    return true;
}

proto_can_parse_result_t proto_can_parse_frame(const char *text, proto_can_frame_t *frame)
{
    if (!text || !frame) { return PROTO_CAN_PARSE_BAD_FORMAT; }
    const char *hash = strchr(text, '#');
    if (!hash) { return PROTO_CAN_PARSE_BAD_FORMAT; }
    const size_t id_len = (size_t)(hash - text);
    if (id_len != 3 && id_len != 8) { return PROTO_CAN_PARSE_BAD_ID; }

    uint32_t id = 0;
    for (size_t i = 0; i < id_len; ++i) {
        const int v = hex_val(text[i]);
        if (v < 0) { return PROTO_CAN_PARSE_BAD_ID; }
        id = (id << 4) | (uint32_t)v;
    }
    proto_can_frame_t out;
    memset(&out, 0, sizeof(out));
    out.id = id;
    out.extended = (id_len == 8);
    if (id > (out.extended ? PROTO_CAN_EXT_ID_MAX : PROTO_CAN_STD_ID_MAX)) {
        return PROTO_CAN_PARSE_BAD_ID;
    }

    const char *payload = hash + 1;
    const size_t n = strlen(payload);
    if (n == 1 && (payload[0] == 'R' || payload[0] == 'r')) {
        out.rtr = true;
    } else {
        if ((n % 2) != 0 || n > 2 * PROTO_CAN_MAX_DLC) { return PROTO_CAN_PARSE_BAD_DATA; }
        for (size_t i = 0; i < n; i += 2) {
            const int hi = hex_val(payload[i]);
            const int lo = hex_val(payload[i + 1]);
            if (hi < 0 || lo < 0) { return PROTO_CAN_PARSE_BAD_DATA; }
            out.data[i / 2] = (uint8_t)((hi << 4) | lo);
        }
        out.dlc = (uint8_t)(n / 2);
    }
    *frame = out;
    return PROTO_CAN_PARSE_OK;
}

size_t proto_can_format_frame(const proto_can_frame_t *frame, char *out, size_t out_size)
{
    static const char digits[] = "0123456789ABCDEF";
    if (!out || !proto_can_frame_valid(frame)) { return 0; }
    const size_t id_digits = frame->extended ? 8 : 3;
    const size_t needed = id_digits + 1 + (frame->rtr ? 1 : (size_t)frame->dlc * 2) + 1;
    if (out_size < needed) { return 0; }

    size_t pos = 0;
    for (size_t i = 0; i < id_digits; ++i) {
        out[pos++] = digits[(frame->id >> (4 * (id_digits - 1 - i))) & 0xF];
    }
    out[pos++] = '#';
    if (frame->rtr) {
        out[pos++] = 'R';
    } else {
        for (size_t i = 0; i < frame->dlc; ++i) {
            out[pos++] = digits[frame->data[i] >> 4];
            out[pos++] = digits[frame->data[i] & 0xF];
        }
    }
    out[pos] = '\0';
    return pos;
}
