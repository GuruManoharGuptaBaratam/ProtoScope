#include "proto_log_format.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "proto_event_caps.h"
#include "proto_hex.h"

size_t proto_log_format_header(const char *firmware, char *out, size_t out_size)
{
    if (!out || out_size == 0) { return 0; }
    const int n = snprintf(out, out_size,
        "# ProtoScope log schema=%d firmware=%s\n"
        "# timestamp_us = microseconds since boot (no calendar clock)\n"
        "timestamp_us,event_id,correlation_id,protocol,direction,status,error_code,hop,"
        "origin_route,length,data_hex,meta\n",
        PROTO_LOG_SCHEMA_VERSION, firmware ? firmware : "unknown");
    if (n < 0 || (size_t)n >= out_size) { out[0] = '\0'; return 0; }
    return (size_t)n;
}

static void format_meta(const proto_event_t *e, char *meta, size_t size)
{
    switch (e->protocol) {
    case PROTO_PROTOCOL_UART:
    case PROTO_PROTOCOL_RS485:
        snprintf(meta, size, "baud=%" PRIu32 ";term=%s", e->meta.serial.baud_rate,
                 e->meta.serial.terminator == PROTO_TERM_CRLF ? "CRLF" :
                 e->meta.serial.terminator == PROTO_TERM_LF ? "LF" : "NONE");
        break;
    case PROTO_PROTOCOL_CAN:
        snprintf(meta, size, "id=%" PRIX32 ";ext=%d;rtr=%d;dlc=%u", e->meta.can.id,
                 e->meta.can.extended, e->meta.can.rtr, (unsigned)e->meta.can.dlc);
        break;
    case PROTO_PROTOCOL_I2C:
        snprintf(meta, size, "addr=0x%02X;op=%s;wr=%u;rd=%u;ack=%s", (unsigned)e->meta.i2c.address,
                 e->meta.i2c.op == PROTO_I2C_OP_WRITE ? "WRITE" :
                 e->meta.i2c.op == PROTO_I2C_OP_READ ? "READ" : "WRITE_READ",
                 (unsigned)e->meta.i2c.write_length, (unsigned)e->meta.i2c.read_length,
                 e->meta.i2c.ack == PROTO_I2C_ACK_ACK ? "ACK" :
                 e->meta.i2c.ack == PROTO_I2C_ACK_NACK ? "NACK" : "UNKNOWN");
        break;
    case PROTO_PROTOCOL_SPI:
        snprintf(meta, size, "cs=%u;mode=%u;clock_hz=%" PRIu32 ";len=%u",
                 (unsigned)e->meta.spi.chip_select, (unsigned)e->meta.spi.mode,
                 e->meta.spi.clock_hz, (unsigned)e->meta.spi.transaction_length);
        break;
    case PROTO_PROTOCOL_COUNT:
        meta[0] = '\0';
        break;
    }
}

size_t proto_log_format_record(const proto_event_t *event, char *out, size_t out_size)
{
    if (!out || out_size == 0) { return 0; }
    out[0] = '\0';
    if (!event || (unsigned)event->protocol >= PROTO_PROTOCOL_COUNT ||
        event->payload_length > PROTO_EVENT_MAX_PAYLOAD) {
        return 0;
    }
    char hex[2 * PROTO_EVENT_MAX_PAYLOAD + 1];
    char meta[112];
    (void)proto_hex_format(event->payload, event->payload_length, hex, sizeof(hex));
    format_meta(event, meta, sizeof(meta));
    const int n = snprintf(out, out_size,
        "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%s,%s,%d,%u,%u,%u,%s,%s\n",
        event->timestamp_us, event->event_id, event->correlation_id,
        proto_protocol_name(event->protocol), event->direction == PROTO_DIR_TX ? "TX" : "RX",
        proto_event_status_name(event->status), (int)event->error_code, (unsigned)event->hop_count,
        (unsigned)event->origin_route_id, (unsigned)event->payload_length, hex, meta);
    if (n < 0 || (size_t)n >= out_size) { out[0] = '\0'; return 0; }
    return (size_t)n;
}

bool proto_log_parse_index(const char *name, uint16_t *index)
{
    if (!name || !index || strlen(name) != 11) { return false; }
    if (toupper((unsigned char)name[0]) != 'L' || toupper((unsigned char)name[1]) != 'O' ||
        toupper((unsigned char)name[2]) != 'G') {
        return false;
    }
    unsigned value = 0;
    for (int i = 3; i < 7; ++i) {
        if (!isdigit((unsigned char)name[i])) { return false; }
        value = value * 10 + (unsigned)(name[i] - '0');
    }
    if (name[7] != '.' || toupper((unsigned char)name[8]) != 'C' ||
        toupper((unsigned char)name[9]) != 'S' || toupper((unsigned char)name[10]) != 'V') {
        return false;
    }
    if (value < 1 || value > PROTO_LOG_INDEX_MAX) { return false; }
    *index = (uint16_t)value;
    return true;
}

void proto_log_make_name(uint16_t index, char *out, size_t out_size)
{
    if (!out || out_size == 0) { return; }
    snprintf(out, out_size, "LOG%04u.CSV", (unsigned)index);
}
