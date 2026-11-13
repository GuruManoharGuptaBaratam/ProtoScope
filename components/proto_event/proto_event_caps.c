#include "proto_event_caps.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    uint32_t caps;
    size_t max_payload;
} k_table[PROTO_PROTOCOL_COUNT] = {
    [PROTO_PROTOCOL_UART] = { "UART",
        PROTO_CAP_RX_EVENT | PROTO_CAP_TX | PROTO_CAP_BYTE_STREAM, PROTO_UART_FRAME_MAX },
    [PROTO_PROTOCOL_RS485] = { "RS485",
        PROTO_CAP_RX_EVENT | PROTO_CAP_TX | PROTO_CAP_BYTE_STREAM | PROTO_CAP_HALF_DUPLEX,
        PROTO_UART_FRAME_MAX },
    [PROTO_PROTOCOL_CAN] = { "CAN",
        PROTO_CAP_RX_EVENT | PROTO_CAP_TX | PROTO_CAP_FRAME_ORIENTED, PROTO_CAN_MAX_DLC },
    [PROTO_PROTOCOL_I2C] = { "I2C",
        PROTO_CAP_TX | PROTO_CAP_CONTROLLER_TRANSACTION | PROTO_CAP_CONTROLLER_ROLE,
        PROTO_I2C_MAX_TRANSFER },
    [PROTO_PROTOCOL_SPI] = { "SPI",
        PROTO_CAP_TX | PROTO_CAP_CONTROLLER_TRANSACTION | PROTO_CAP_CONTROLLER_ROLE |
        PROTO_CAP_FULL_DUPLEX_TRANSFER, PROTO_SPI_MAX_TRANSFER },
};

uint32_t proto_protocol_caps(proto_protocol_t protocol)
{
    return (unsigned)protocol < PROTO_PROTOCOL_COUNT ? k_table[protocol].caps : 0;
}

const char *proto_protocol_name(proto_protocol_t protocol)
{
    return (unsigned)protocol < PROTO_PROTOCOL_COUNT ? k_table[protocol].name : "?";
}

size_t proto_protocol_max_payload(proto_protocol_t protocol)
{
    return (unsigned)protocol < PROTO_PROTOCOL_COUNT ? k_table[protocol].max_payload : 0;
}

bool proto_protocol_parse(const char *name, proto_protocol_t *protocol)
{
    if (!name || !protocol) { return false; }
    char normalized[8];
    size_t n = 0;
    for (const char *p = name; *p; ++p) {
        if (*p == '-') { continue; }
        if (n + 1 >= sizeof(normalized)) { return false; }
        normalized[n++] = (char)toupper((unsigned char)*p);
    }
    normalized[n] = '\0';
    for (int i = 0; i < PROTO_PROTOCOL_COUNT; ++i) {
        if (strcmp(normalized, k_table[i].name) == 0) {
            *protocol = (proto_protocol_t)i;
            return true;
        }
    }
    return false;
}

size_t proto_caps_format(uint32_t caps, char *out, size_t out_size)
{
    static const struct { uint32_t flag; const char *name; } flags[] = {
        { PROTO_CAP_RX_EVENT, "RX_EVENT" }, { PROTO_CAP_TX, "TX" },
        { PROTO_CAP_CONTROLLER_TRANSACTION, "CONTROLLER_TXN" },
        { PROTO_CAP_BYTE_STREAM, "BYTE_STREAM" }, { PROTO_CAP_FRAME_ORIENTED, "FRAME" },
        { PROTO_CAP_HALF_DUPLEX, "HALF_DUPLEX" },
        { PROTO_CAP_FULL_DUPLEX_TRANSFER, "FULL_DUPLEX" },
        { PROTO_CAP_CONTROLLER_ROLE, "CONTROLLER_ONLY" },
    };
    if (!out || out_size == 0) { return 0; }
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        if (!(caps & flags[i].flag)) { continue; }
        const int written = snprintf(out + pos, out_size - pos, "%s%s", pos ? "|" : "",
                                     flags[i].name);
        if (written < 0 || (size_t)written >= out_size - pos) { break; }
        pos += (size_t)written;
    }
    return pos;
}
