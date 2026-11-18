#include "proto_route.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto_event_caps.h"
#include "proto_hex.h"

/* ---- structural default profiles (device-level details are TBD) ----------------- */
static const struct {
    proto_protocol_t source;
    proto_protocol_t destination;
    proto_decoder_t decoder;
    proto_encoder_t encoder;
} k_defaults[PROTO_ROUTE_COUNT] = {
    /* RT-01 */ { PROTO_PROTOCOL_UART,  PROTO_PROTOCOL_CAN,   PROTO_DEC_PS_CAN_V1, PROTO_ENC_CAN },
    /* RT-02 */ { PROTO_PROTOCOL_CAN,   PROTO_PROTOCOL_UART,  PROTO_DEC_RAW,       PROTO_ENC_SERIAL_CAN_LINE },
    /* RT-03 */ { PROTO_PROTOCOL_UART,  PROTO_PROTOCOL_RS485, PROTO_DEC_RAW,       PROTO_ENC_SERIAL_RAW },
    /* RT-04 */ { PROTO_PROTOCOL_RS485, PROTO_PROTOCOL_UART,  PROTO_DEC_RAW,       PROTO_ENC_SERIAL_RAW },
    /* RT-05 */ { PROTO_PROTOCOL_CAN,   PROTO_PROTOCOL_RS485, PROTO_DEC_RAW,       PROTO_ENC_SERIAL_CAN_LINE },
    /* RT-06 */ { PROTO_PROTOCOL_RS485, PROTO_PROTOCOL_CAN,   PROTO_DEC_PS_CAN_V1, PROTO_ENC_CAN },
    /* RT-07 */ { PROTO_PROTOCOL_UART,  PROTO_PROTOCOL_I2C,   PROTO_DEC_HEX_LINE,  PROTO_ENC_I2C_WRITE },
    /* RT-08 */ { PROTO_PROTOCOL_I2C,   PROTO_PROTOCOL_UART,  PROTO_DEC_RAW,       PROTO_ENC_SERIAL_HEX },
    /* RT-09 */ { PROTO_PROTOCOL_CAN,   PROTO_PROTOCOL_I2C,   PROTO_DEC_RAW,       PROTO_ENC_I2C_WRITE },
    /* RT-10 */ { PROTO_PROTOCOL_I2C,   PROTO_PROTOCOL_CAN,   PROTO_DEC_RAW,       PROTO_ENC_CAN },
    /* RT-11 */ { PROTO_PROTOCOL_RS485, PROTO_PROTOCOL_I2C,   PROTO_DEC_HEX_LINE,  PROTO_ENC_I2C_WRITE },
    /* RT-12 */ { PROTO_PROTOCOL_I2C,   PROTO_PROTOCOL_RS485, PROTO_DEC_RAW,       PROTO_ENC_SERIAL_HEX },
    /* RT-13 */ { PROTO_PROTOCOL_UART,  PROTO_PROTOCOL_SPI,   PROTO_DEC_HEX_LINE,  PROTO_ENC_SPI_XFER },
    /* RT-14 */ { PROTO_PROTOCOL_SPI,   PROTO_PROTOCOL_UART,  PROTO_DEC_RAW,       PROTO_ENC_SERIAL_HEX },
    /* RT-15 */ { PROTO_PROTOCOL_CAN,   PROTO_PROTOCOL_SPI,   PROTO_DEC_RAW,       PROTO_ENC_SPI_XFER },
    /* RT-16 */ { PROTO_PROTOCOL_SPI,   PROTO_PROTOCOL_CAN,   PROTO_DEC_RAW,       PROTO_ENC_CAN },
    /* RT-17 */ { PROTO_PROTOCOL_RS485, PROTO_PROTOCOL_SPI,   PROTO_DEC_HEX_LINE,  PROTO_ENC_SPI_XFER },
    /* RT-18 */ { PROTO_PROTOCOL_SPI,   PROTO_PROTOCOL_RS485, PROTO_DEC_RAW,       PROTO_ENC_SERIAL_HEX },
    /* RT-19 */ { PROTO_PROTOCOL_I2C,   PROTO_PROTOCOL_SPI,   PROTO_DEC_RAW,       PROTO_ENC_SPI_XFER },
    /* RT-20 */ { PROTO_PROTOCOL_SPI,   PROTO_PROTOCOL_I2C,   PROTO_DEC_RAW,       PROTO_ENC_I2C_WRITE },
};

static bool is_serial(proto_protocol_t p)
{
    return p == PROTO_PROTOCOL_UART || p == PROTO_PROTOCOL_RS485;
}

static bool is_polled(proto_protocol_t p)
{
    return p == PROTO_PROTOCOL_I2C || p == PROTO_PROTOCOL_SPI;
}

void proto_route_name(uint8_t id, char *out, size_t out_size)
{
    if (!out || out_size == 0) { return; }
    snprintf(out, out_size, "RT-%02u", (unsigned)id);
}

void proto_route_defaults(proto_route_t *route, uint8_t id)
{
    if (!route) { return; }
    memset(route, 0, sizeof(*route));
    route->id = id;
    if (id < 1 || id > PROTO_ROUTE_COUNT) { return; }
    const size_t i = (size_t)id - 1;
    route->source = k_defaults[i].source;
    route->destination = k_defaults[i].destination;
    route->decoder = k_defaults[i].decoder;
    route->encoder = k_defaults[i].encoder;
    route->mapper = PROTO_MAP_PASS;
    route->terminator = PROTO_TERM_LF;
    route->policy = PROTO_POLICY_DROP;
    route->hold_timeout_ms = PROTO_ROUTE_DEFAULT_HOLD_MS;
    route->scale = (proto_scale_t){ .in_width = 1, .in_big_endian = true, .mul = 1, .div = 1,
                                    .out_width = 1, .out_big_endian = true };
    /* Only RT-01 has a fully specified grammar (route-matrix.md section 3). */
    route->can_id_from_datum = (id == 1);
}

const char *proto_route_check_name(proto_route_check_t check)
{
    switch (check) {
    case PROTO_ROUTE_OK: return "ready";
    case PROTO_ROUTE_ERR_ID: return "bad_route_id";
    case PROTO_ROUTE_ERR_SAME_INTERFACE: return "same_interface";
    case PROTO_ROUTE_ERR_FILTER_MISSING: return "can_filter_not_set";
    case PROTO_ROUTE_ERR_POLL_MISSING: return "source_read_not_configured";
    case PROTO_ROUTE_ERR_DECODER: return "decoder_incompatible_with_source";
    case PROTO_ROUTE_ERR_MAPPER: return "mapper_parameters_invalid";
    case PROTO_ROUTE_ERR_ENCODER: return "encoder_incompatible_with_destination";
    case PROTO_ROUTE_ERR_DEST_PARAM: return "destination_not_configured";
    case PROTO_ROUTE_ERR_POLICY: return "policy_invalid";
    }
    return "?";
}

const char *proto_proc_result_name(proto_proc_result_t result)
{
    switch (result) {
    case PROTO_PROC_OK: return "ok";
    case PROTO_PROC_DECODE_FORMAT: return "decode_format";
    case PROTO_PROC_DECODE_ID: return "decode_id";
    case PROTO_PROC_DECODE_DLC: return "decode_dlc";
    case PROTO_PROC_DECODE_PAYLOAD: return "decode_payload";
    case PROTO_PROC_DECODE_LENGTH: return "decode_length";
    case PROTO_PROC_MAP_LENGTH: return "map_length";
    case PROTO_PROC_MAP_RANGE: return "map_range";
    case PROTO_PROC_ENCODE_LENGTH: return "encode_length";
    case PROTO_PROC_ENCODE_PARAM: return "encode_param";
    case PROTO_PROC_ENCODE_UNREPRESENTABLE: return "encode_unrepresentable";
    }
    return "?";
}

static bool scale_valid(const proto_scale_t *s)
{
    const bool w_in = s->in_width == 1 || s->in_width == 2 || s->in_width == 4;
    const bool w_out = s->out_width == 1 || s->out_width == 2 || s->out_width == 4;
    return w_in && w_out && s->div > 0;
}

proto_route_check_t proto_route_validate(const proto_route_t *route)
{
    if (!route || route->id < 1 || route->id > PROTO_ROUTE_COUNT) { return PROTO_ROUTE_ERR_ID; }
    if ((unsigned)route->source >= PROTO_PROTOCOL_COUNT ||
        (unsigned)route->destination >= PROTO_PROTOCOL_COUNT) {
        return PROTO_ROUTE_ERR_ID;
    }
    if (route->source == route->destination) { return PROTO_ROUTE_ERR_SAME_INTERFACE; }
    if (route->source == PROTO_PROTOCOL_CAN && !route->has_filter) {
        return PROTO_ROUTE_ERR_FILTER_MISSING;
    }
    if (is_polled(route->source)) {
        const bool ok = route->source == PROTO_PROTOCOL_I2C ?
            (route->poll_configured && route->poll_i2c.op != PROTO_I2C_OP_WRITE &&
             proto_i2c_txn_valid(&route->poll_i2c)) :
            (route->poll_configured && route->poll_spi_length >= 1 &&
             proto_spi_transfer_length_valid(route->poll_spi_length));
        if (!ok) { return PROTO_ROUTE_ERR_POLL_MISSING; }
    }
    if ((route->decoder == PROTO_DEC_HEX_LINE || route->decoder == PROTO_DEC_PS_CAN_V1) &&
        !is_serial(route->source)) {
        return PROTO_ROUTE_ERR_DECODER;
    }
    if ((unsigned)route->decoder > PROTO_DEC_PS_CAN_V1) { return PROTO_ROUTE_ERR_DECODER; }
    if (route->mapper == PROTO_MAP_SLICE && route->slice_length < 1) { return PROTO_ROUTE_ERR_MAPPER; }
    if (route->mapper == PROTO_MAP_SCALE && !scale_valid(&route->scale)) { return PROTO_ROUTE_ERR_MAPPER; }
    if ((unsigned)route->mapper > PROTO_MAP_SCALE) { return PROTO_ROUTE_ERR_MAPPER; }
    switch (route->encoder) {
    case PROTO_ENC_SERIAL_RAW:
    case PROTO_ENC_SERIAL_HEX:
        if (!is_serial(route->destination)) { return PROTO_ROUTE_ERR_ENCODER; }
        break;
    case PROTO_ENC_SERIAL_CAN_LINE:
        if (!is_serial(route->destination) || route->source != PROTO_PROTOCOL_CAN ||
            route->decoder != PROTO_DEC_RAW) {
            return PROTO_ROUTE_ERR_ENCODER;
        }
        break;
    case PROTO_ENC_CAN:
        if (route->destination != PROTO_PROTOCOL_CAN) { return PROTO_ROUTE_ERR_ENCODER; }
        if (route->can_id_from_datum) {
            if (route->decoder != PROTO_DEC_PS_CAN_V1) { return PROTO_ROUTE_ERR_DEST_PARAM; }
        } else {
            proto_can_frame_t f = { .id = route->can_id, .extended = route->can_extended };
            if (!route->can_id_set || !proto_can_frame_valid(&f)) { return PROTO_ROUTE_ERR_DEST_PARAM; }
        }
        break;
    case PROTO_ENC_I2C_WRITE:
        if (route->destination != PROTO_PROTOCOL_I2C) { return PROTO_ROUTE_ERR_ENCODER; }
        if (!proto_i2c_address_valid(route->i2c_address) || !route->prefix_set) {
            return PROTO_ROUTE_ERR_DEST_PARAM;
        }
        break;
    case PROTO_ENC_SPI_XFER:
        if (route->destination != PROTO_PROTOCOL_SPI) { return PROTO_ROUTE_ERR_ENCODER; }
        if (!route->prefix_set) { return PROTO_ROUTE_ERR_DEST_PARAM; }
        break;
    default:
        return PROTO_ROUTE_ERR_ENCODER;
    }
    if (route->prefix_length > PROTO_ROUTE_PREFIX_MAX) { return PROTO_ROUTE_ERR_DEST_PARAM; }
    if ((unsigned)route->terminator > PROTO_TERM_CRLF) { return PROTO_ROUTE_ERR_ENCODER; }
    if ((unsigned)route->policy > PROTO_POLICY_HOLD || route->hold_timeout_ms > 10000) {
        return PROTO_ROUTE_ERR_POLICY;
    }
    return PROTO_ROUTE_OK;
}

/* ---- key=value configuration ----------------------------------------------------- */
static bool parse_u32(const char *text, int base, uint32_t max, uint32_t *value)
{
    if (!text || !*text) { return false; }
    char *end = NULL;
    const unsigned long long v = strtoull(text, &end, base);
    if (*end != '\0' || v > max) { return false; }
    *value = (uint32_t)v;
    return true;
}

static bool parse_i32(const char *text, int32_t *value)
{
    if (!text || !*text) { return false; }
    char *end = NULL;
    const long long v = strtoll(text, &end, 10);
    if (*end != '\0' || v < INT32_MIN || v > INT32_MAX) { return false; }
    *value = (int32_t)v;
    return true;
}

static bool parse_bool(const char *text, bool *value)
{
    if (!text) { return false; }
    if (strcmp(text, "1") == 0) { *value = true; return true; }
    if (strcmp(text, "0") == 0) { *value = false; return true; }
    return false;
}

/* Splits "a:b:c" in place into at most max fields. Returns the field count. */
static size_t split_colon(char *text, char **fields, size_t max)
{
    size_t n = 0;
    char *p = text;
    while (n < max) {
        fields[n++] = p;
        char *colon = strchr(p, ':');
        if (!colon) { break; }
        *colon = '\0';
        p = colon + 1;
    }
    return n;
}

static bool parse_width(const char *t, uint8_t *w)
{
    uint32_t v = 0;
    if (!parse_u32(t, 10, 4, &v) || (v != 1 && v != 2 && v != 4)) { return false; }
    *w = (uint8_t)v;
    return true;
}

static bool parse_endian(const char *t, bool *big)
{
    if (strcmp(t, "be") == 0) { *big = true; return true; }
    if (strcmp(t, "le") == 0) { *big = false; return true; }
    return false;
}

static bool parse_sign(const char *t, bool *is_signed)
{
    if (strcmp(t, "s") == 0) { *is_signed = true; return true; }
    if (strcmp(t, "u") == 0) { *is_signed = false; return true; }
    return false;
}

proto_route_set_result_t proto_route_set(proto_route_t *route, const char *key, const char *value)
{
    if (!route || !key || !value) { return PROTO_SET_BAD_VALUE; }
    uint32_t n = 0;
    char buffer[160];
    if (strlen(value) >= sizeof(buffer)) { return PROTO_SET_BAD_VALUE; }
    strcpy(buffer, value);

    if (strcmp(key, "filter") == 0) {
        uint32_t id = 0, mask = 0;
        char *slash = strchr(buffer, '/');
        if (!slash) { return PROTO_SET_BAD_VALUE; }
        *slash = '\0';
        if (!parse_u32(buffer, 16, PROTO_CAN_EXT_ID_MAX, &id) ||
            !parse_u32(slash + 1, 16, PROTO_CAN_EXT_ID_MAX, &mask)) {
            return PROTO_SET_BAD_VALUE;
        }
        route->has_filter = true; route->filter_id = id; route->filter_mask = mask;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "filter_ext") == 0) {
        if (!parse_bool(value, &route->filter_extended)) { return PROTO_SET_BAD_VALUE; }
        return PROTO_SET_OK;
    }
    if (strcmp(key, "dec") == 0) {
        if (strcmp(value, "raw") == 0) { route->decoder = PROTO_DEC_RAW; }
        else if (strcmp(value, "hex") == 0) { route->decoder = PROTO_DEC_HEX_LINE; }
        else if (strcmp(value, "ps_can") == 0) { route->decoder = PROTO_DEC_PS_CAN_V1; }
        else { return PROTO_SET_BAD_VALUE; }
        return PROTO_SET_OK;
    }
    if (strcmp(key, "enc") == 0) {
        if (strcmp(value, "raw") == 0) { route->encoder = PROTO_ENC_SERIAL_RAW; }
        else if (strcmp(value, "hex") == 0) { route->encoder = PROTO_ENC_SERIAL_HEX; }
        else if (strcmp(value, "can_line") == 0) { route->encoder = PROTO_ENC_SERIAL_CAN_LINE; }
        else if (strcmp(value, "can") == 0) { route->encoder = PROTO_ENC_CAN; }
        else if (strcmp(value, "i2c") == 0) { route->encoder = PROTO_ENC_I2C_WRITE; }
        else if (strcmp(value, "spi") == 0) { route->encoder = PROTO_ENC_SPI_XFER; }
        else { return PROTO_SET_BAD_VALUE; }
        return PROTO_SET_OK;
    }
    if (strcmp(key, "term") == 0) {
        if (strcmp(value, "none") == 0) { route->terminator = PROTO_TERM_NONE; }
        else if (strcmp(value, "lf") == 0) { route->terminator = PROTO_TERM_LF; }
        else if (strcmp(value, "crlf") == 0) { route->terminator = PROTO_TERM_CRLF; }
        else { return PROTO_SET_BAD_VALUE; }
        return PROTO_SET_OK;
    }
    if (strcmp(key, "map") == 0) {
        if (strcmp(value, "pass") == 0) { route->mapper = PROTO_MAP_PASS; }
        else if (strcmp(value, "slice") == 0) { route->mapper = PROTO_MAP_SLICE; }
        else if (strcmp(value, "scale") == 0) { route->mapper = PROTO_MAP_SCALE; }
        else { return PROTO_SET_BAD_VALUE; }
        return PROTO_SET_OK;
    }
    if (strcmp(key, "slice") == 0) { /* OFFSET:LENGTH */
        char *f[2];
        uint32_t off = 0, len = 0;
        if (split_colon(buffer, f, 2) != 2 || !parse_u32(f[0], 10, 63, &off) ||
            !parse_u32(f[1], 10, 64, &len) || len < 1) {
            return PROTO_SET_BAD_VALUE;
        }
        route->mapper = PROTO_MAP_SLICE; route->slice_offset = (uint8_t)off;
        route->slice_length = (uint8_t)len;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "scale") == 0) { /* INW:be|le:s|u:MUL:DIV:ADD:OUTW:be|le:s|u */
        char *f[9];
        proto_scale_t s;
        memset(&s, 0, sizeof(s));
        if (split_colon(buffer, f, 9) != 9 || !parse_width(f[0], &s.in_width) ||
            !parse_endian(f[1], &s.in_big_endian) || !parse_sign(f[2], &s.in_signed) ||
            !parse_i32(f[3], &s.mul) || !parse_i32(f[4], &s.div) || s.div <= 0 ||
            !parse_i32(f[5], &s.add) || !parse_width(f[6], &s.out_width) ||
            !parse_endian(f[7], &s.out_big_endian) || !parse_sign(f[8], &s.out_signed)) {
            return PROTO_SET_BAD_VALUE;
        }
        route->mapper = PROTO_MAP_SCALE; route->scale = s;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "can_id") == 0) {
        if (strcmp(value, "datum") == 0) {
            route->can_id_from_datum = true; route->can_id_set = false;
            return PROTO_SET_OK;
        }
        if (!parse_u32(value, 16, PROTO_CAN_EXT_ID_MAX, &n)) { return PROTO_SET_BAD_VALUE; }
        route->can_id = n; route->can_id_set = true; route->can_id_from_datum = false;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "can_ext") == 0) {
        if (!parse_bool(value, &route->can_extended)) { return PROTO_SET_BAD_VALUE; }
        return PROTO_SET_OK;
    }
    if (strcmp(key, "i2c_addr") == 0) {
        if (!parse_u32(value, 16, 0x7F, &n) || !proto_i2c_address_valid((uint8_t)n)) {
            return PROTO_SET_BAD_VALUE;
        }
        route->i2c_address = (uint8_t)n;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "prefix") == 0) {
        if (strcmp(value, "none") == 0) {
            route->prefix_length = 0; route->prefix_set = true;
            return PROTO_SET_OK;
        }
        size_t len = 0;
        if (!proto_hex_parse(value, route->prefix, PROTO_ROUTE_PREFIX_MAX, &len)) {
            return PROTO_SET_BAD_VALUE;
        }
        route->prefix_length = (uint8_t)len; route->prefix_set = true;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "poll_i2c") == 0) { /* ADDR:WRITEHEX:READLEN, WRITEHEX may be empty (plain read) */
        char *f[3];
        uint32_t addr = 0, len = 0;
        proto_i2c_txn_t txn;
        memset(&txn, 0, sizeof(txn));
        if (split_colon(buffer, f, 3) != 3 || !parse_u32(f[0], 16, 0x7F, &addr) ||
            !parse_u32(f[2], 10, 64, &len) || len < 1) {
            return PROTO_SET_BAD_VALUE;
        }
        size_t wlen = 0;
        if (f[1][0] != '\0' && !proto_hex_parse(f[1], txn.write_data, sizeof(txn.write_data), &wlen)) {
            return PROTO_SET_BAD_VALUE;
        }
        txn.address = (uint8_t)addr; txn.write_length = (uint8_t)wlen; txn.read_length = (uint8_t)len;
        txn.op = wlen ? PROTO_I2C_OP_WRITE_READ : PROTO_I2C_OP_READ;
        if (!proto_i2c_txn_valid(&txn)) { return PROTO_SET_BAD_VALUE; }
        route->poll_i2c = txn; route->poll_configured = true;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "poll_spi") == 0) {
        size_t len = 0;
        if (!proto_hex_parse(value, route->poll_spi, sizeof(route->poll_spi), &len) ||
            !proto_spi_transfer_length_valid(len)) {
            return PROTO_SET_BAD_VALUE;
        }
        route->poll_spi_length = (uint8_t)len; route->poll_configured = true;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "interval") == 0) {
        if (!parse_u32(value, 10, 3600000, &n)) { return PROTO_SET_BAD_VALUE; }
        route->poll_interval_ms = n;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "policy") == 0) {
        if (strcmp(value, "drop") == 0) { route->policy = PROTO_POLICY_DROP; }
        else if (strcmp(value, "hold") == 0) { route->policy = PROTO_POLICY_HOLD; }
        else { return PROTO_SET_BAD_VALUE; }
        return PROTO_SET_OK;
    }
    if (strcmp(key, "hold_ms") == 0) {
        if (!parse_u32(value, 10, 10000, &n)) { return PROTO_SET_BAD_VALUE; }
        route->hold_timeout_ms = n;
        return PROTO_SET_OK;
    }
    if (strcmp(key, "src_dev") == 0 || strcmp(key, "dst_dev") == 0) {
        if (!parse_u32(value, 10, 255, &n)) { return PROTO_SET_BAD_VALUE; }
        if (key[0] == 's') { route->source_device = (uint8_t)n; } else { route->destination_device = (uint8_t)n; }
        return PROTO_SET_OK;
    }
    return PROTO_SET_UNKNOWN_KEY;
}

/* ---- matching and decoding ---------------------------------------------------------- */
bool proto_route_match(const proto_route_t *route, const proto_event_t *source)
{
    if (!route || !source || source->protocol != route->source) { return false; }
    if (source->direction != PROTO_DIR_RX || source->status != PROTO_EVENT_STATUS_OK) { return false; }
    if (route->source == PROTO_PROTOCOL_CAN) {
        if (!route->has_filter) { return false; }
        if (source->meta.can.extended != route->filter_extended) { return false; }
        return (source->meta.can.id & route->filter_mask) == (route->filter_id & route->filter_mask);
    }
    return true;
}

static size_t serial_body_length(const proto_event_t *event)
{
    size_t n = event->payload_length;
    if (event->meta.serial.terminator == PROTO_TERM_CRLF && n >= 2) { return n - 2; }
    if (event->meta.serial.terminator == PROTO_TERM_LF && n >= 1) { return n - 1; }
    return n;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

/* RT-01 grammar: CAN,<id_hex 3>,<dlc 0-8>,<payload_hex 2*dlc>  (no whitespace, no extra fields). */
static proto_proc_result_t decode_ps_can_v1(const uint8_t *line, size_t length, proto_datum_t *datum)
{
    if (length < 4 || memcmp(line, "CAN,", 4) != 0) { return PROTO_PROC_DECODE_FORMAT; }
    for (size_t i = 0; i < length; ++i) {
        if (line[i] <= ' ' || line[i] >= 0x7F) { return PROTO_PROC_DECODE_FORMAT; } /* no whitespace/control */
    }
    const uint8_t *p = line + 4;
    const uint8_t *end = line + length;
    const uint8_t *comma = memchr(p, ',', (size_t)(end - p));
    if (!comma) { return PROTO_PROC_DECODE_FORMAT; }
    if (comma - p != 3) { return PROTO_PROC_DECODE_ID; }
    uint32_t id = 0;
    for (int i = 0; i < 3; ++i) {
        const int v = hex_digit((char)p[i]);
        if (v < 0) { return PROTO_PROC_DECODE_ID; }
        id = (id << 4) | (uint32_t)v;
    }
    if (id > PROTO_CAN_STD_ID_MAX) { return PROTO_PROC_DECODE_ID; }

    p = comma + 1;
    comma = memchr(p, ',', (size_t)(end - p));
    if (!comma) { return PROTO_PROC_DECODE_FORMAT; }
    const size_t dlc_digits = (size_t)(comma - p);
    if (dlc_digits < 1 || dlc_digits > 2) { return PROTO_PROC_DECODE_DLC; }
    uint32_t dlc = 0;
    for (size_t i = 0; i < dlc_digits; ++i) {
        if (p[i] < '0' || p[i] > '9') { return PROTO_PROC_DECODE_DLC; }
        dlc = dlc * 10 + (uint32_t)(p[i] - '0');
    }
    if (dlc > PROTO_CAN_MAX_DLC) { return PROTO_PROC_DECODE_DLC; }

    p = comma + 1;
    if (memchr(p, ',', (size_t)(end - p))) { return PROTO_PROC_DECODE_FORMAT; } /* extra field */
    const size_t hex_chars = (size_t)(end - p);
    for (size_t i = 0; i < hex_chars; ++i) {
        if (hex_digit((char)p[i]) < 0) { return PROTO_PROC_DECODE_PAYLOAD; }
    }
    if (hex_chars != 2 * (size_t)dlc) { return PROTO_PROC_DECODE_LENGTH; }
    memset(datum, 0, sizeof(*datum));
    for (size_t i = 0; i < dlc; ++i) {
        datum->data[i] = (uint8_t)((hex_digit((char)p[2 * i]) << 4) | hex_digit((char)p[2 * i + 1]));
    }
    datum->length = (uint16_t)dlc;
    datum->has_can_id = true;
    datum->can_id = id;
    return PROTO_PROC_OK;
}

proto_proc_result_t proto_route_decode(const proto_route_t *route, const proto_event_t *source,
                                       proto_datum_t *datum)
{
    if (!route || !source || !datum) { return PROTO_PROC_DECODE_FORMAT; }
    memset(datum, 0, sizeof(*datum));
    if (is_serial(source->protocol)) {
        const size_t body = serial_body_length(source);
        switch (route->decoder) {
        case PROTO_DEC_RAW:
            memcpy(datum->data, source->payload, body);
            datum->length = (uint16_t)body;
            return PROTO_PROC_OK;
        case PROTO_DEC_HEX_LINE: {
            char text[2 * PROTO_EVENT_MAX_PAYLOAD + 1];
            if (body == 0 || body >= sizeof(text)) { return PROTO_PROC_DECODE_PAYLOAD; }
            memcpy(text, source->payload, body);
            text[body] = '\0';
            size_t length = 0;
            if (!proto_hex_parse(text, datum->data, sizeof(datum->data), &length)) {
                return PROTO_PROC_DECODE_PAYLOAD;
            }
            datum->length = (uint16_t)length;
            return PROTO_PROC_OK;
        }
        case PROTO_DEC_PS_CAN_V1:
            return decode_ps_can_v1(source->payload, body, datum);
        }
        return PROTO_PROC_DECODE_FORMAT;
    }
    if (route->decoder != PROTO_DEC_RAW) { return PROTO_PROC_DECODE_FORMAT; }
    memcpy(datum->data, source->payload, source->payload_length);
    datum->length = source->payload_length;
    if (source->protocol == PROTO_PROTOCOL_CAN) {
        datum->has_can_id = true;
        datum->can_id = source->meta.can.id;
        datum->can_extended = source->meta.can.extended;
        datum->can_rtr = source->meta.can.rtr;
    }
    return PROTO_PROC_OK;
}

/* ---- mapping ------------------------------------------------------------------------ */
static int64_t read_value(const uint8_t *p, uint8_t width, bool big, bool is_signed)
{
    uint64_t v = 0;
    for (uint8_t i = 0; i < width; ++i) {
        const uint8_t byte = big ? p[i] : p[width - 1 - i];
        v = (v << 8) | byte;
    }
    if (is_signed) {
        const uint64_t sign = 1ULL << (8 * width - 1);
        if (v & sign) { return (int64_t)v - (int64_t)(sign << 1); }
    }
    return (int64_t)v;
}

proto_proc_result_t proto_route_map(const proto_route_t *route, proto_datum_t *datum)
{
    if (!route || !datum) { return PROTO_PROC_MAP_LENGTH; }
    switch (route->mapper) {
    case PROTO_MAP_PASS:
        return PROTO_PROC_OK;
    case PROTO_MAP_SLICE: {
        const size_t end = (size_t)route->slice_offset + route->slice_length;
        if (end > datum->length) { return PROTO_PROC_MAP_LENGTH; }
        memmove(datum->data, datum->data + route->slice_offset, route->slice_length);
        datum->length = route->slice_length;
        return PROTO_PROC_OK;
    }
    case PROTO_MAP_SCALE: {
        const proto_scale_t *s = &route->scale;
        if (!scale_valid(s) || datum->length < s->in_width) { return PROTO_PROC_MAP_LENGTH; }
        int64_t v = read_value(datum->data, s->in_width, s->in_big_endian, s->in_signed);
        v = (v * s->mul) / s->div + s->add;
        const int bits = 8 * s->out_width;
        const int64_t lo = s->out_signed ? -(1LL << (bits - 1)) : 0;
        const int64_t hi = s->out_signed ? (1LL << (bits - 1)) - 1 : (1LL << bits) - 1;
        if (v < lo || v > hi) { return PROTO_PROC_MAP_RANGE; }
        const uint64_t raw = (uint64_t)v & ((bits == 64) ? ~0ULL : ((1ULL << bits) - 1));
        for (uint8_t i = 0; i < s->out_width; ++i) {
            const uint8_t shift = (uint8_t)(8 * (s->out_big_endian ? s->out_width - 1 - i : i));
            datum->data[i] = (uint8_t)(raw >> shift);
        }
        datum->length = s->out_width;
        return PROTO_PROC_OK;
    }
    }
    return PROTO_PROC_MAP_LENGTH;
}

/* ---- encoding ------------------------------------------------------------------------ */
static size_t append_terminator(uint8_t *out, size_t length, proto_serial_terminator_t term)
{
    if (term == PROTO_TERM_LF) { out[length++] = '\n'; }
    if (term == PROTO_TERM_CRLF) { out[length++] = '\r'; out[length++] = '\n'; }
    return length;
}

static proto_proc_result_t finish_serial(const proto_route_t *route, const uint8_t *body,
                                         size_t body_length, proto_event_t *request)
{
    const size_t term = route->terminator == PROTO_TERM_CRLF ? 2 : route->terminator == PROTO_TERM_LF ? 1 : 0;
    if (body_length == 0) { return PROTO_PROC_ENCODE_LENGTH; }
    if (body_length + term > PROTO_UART_FRAME_MAX) { return PROTO_PROC_ENCODE_LENGTH; }
    proto_uart_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    memcpy(frame.data, body, body_length);
    frame.length = append_terminator(frame.data, body_length, route->terminator);
    return proto_event_from_serial(request, route->destination, &frame, 0, PROTO_DIR_TX) ?
           PROTO_PROC_OK : PROTO_PROC_ENCODE_PARAM;
}

proto_proc_result_t proto_route_encode(const proto_route_t *route, const proto_datum_t *datum,
                                       proto_event_t *request)
{
    if (!route || !datum || !request) { return PROTO_PROC_ENCODE_PARAM; }
    uint8_t buffer[PROTO_EVENT_MAX_PAYLOAD + PROTO_ROUTE_PREFIX_MAX];
    switch (route->encoder) {
    case PROTO_ENC_SERIAL_RAW:
        return finish_serial(route, datum->data, datum->length, request);
    case PROTO_ENC_SERIAL_HEX: {
        if (datum->length == 0 || (size_t)datum->length * 2 > PROTO_UART_FRAME_MAX) {
            return PROTO_PROC_ENCODE_LENGTH;
        }
        char hex[2 * PROTO_EVENT_MAX_PAYLOAD + 1];
        const size_t n = proto_hex_format(datum->data, datum->length, hex, sizeof(hex));
        return finish_serial(route, (const uint8_t *)hex, n, request);
    }
    case PROTO_ENC_SERIAL_CAN_LINE: {
        if (!datum->has_can_id) { return PROTO_PROC_ENCODE_PARAM; }
        if (datum->can_rtr) { return PROTO_PROC_ENCODE_UNREPRESENTABLE; }
        char text[96];
        char hex[2 * PROTO_CAN_MAX_DLC + 1];
        if (datum->length > PROTO_CAN_MAX_DLC) { return PROTO_PROC_ENCODE_LENGTH; }
        (void)proto_hex_format(datum->data, datum->length, hex, sizeof(hex));
        const int n = datum->can_extended ?
            snprintf(text, sizeof(text), "CAN,%08" PRIX32 ",%u,%s", datum->can_id, (unsigned)datum->length, hex) :
            snprintf(text, sizeof(text), "CAN,%03" PRIX32 ",%u,%s", datum->can_id, (unsigned)datum->length, hex);
        if (n <= 0 || (size_t)n >= sizeof(text)) { return PROTO_PROC_ENCODE_LENGTH; }
        return finish_serial(route, (const uint8_t *)text, (size_t)n, request);
    }
    case PROTO_ENC_CAN: {
        proto_can_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        if (route->can_id_from_datum) {
            if (!datum->has_can_id) { return PROTO_PROC_ENCODE_PARAM; }
            frame.id = datum->can_id; frame.extended = datum->can_extended;
        } else {
            frame.id = route->can_id; frame.extended = route->can_extended;
        }
        if (datum->length > PROTO_CAN_MAX_DLC) { return PROTO_PROC_ENCODE_LENGTH; }
        frame.dlc = (uint8_t)datum->length;
        memcpy(frame.data, datum->data, datum->length);
        if (!proto_can_frame_valid(&frame)) { return PROTO_PROC_ENCODE_PARAM; }
        return proto_event_from_can(request, &frame, PROTO_DIR_TX) ? PROTO_PROC_OK : PROTO_PROC_ENCODE_PARAM;
    }
    case PROTO_ENC_I2C_WRITE:
    case PROTO_ENC_SPI_XFER: {
        const size_t total = (size_t)route->prefix_length + datum->length;
        if (datum->length == 0 || total > PROTO_I2C_MAX_TRANSFER) { return PROTO_PROC_ENCODE_LENGTH; }
        memcpy(buffer, route->prefix, route->prefix_length);
        memcpy(buffer + route->prefix_length, datum->data, datum->length);
        if (route->encoder == PROTO_ENC_I2C_WRITE) {
            proto_i2c_txn_t txn;
            memset(&txn, 0, sizeof(txn));
            txn.address = route->i2c_address;
            txn.op = PROTO_I2C_OP_WRITE;
            txn.write_length = (uint8_t)total;
            memcpy(txn.write_data, buffer, total);
            return proto_event_from_i2c_request(request, &txn) ? PROTO_PROC_OK : PROTO_PROC_ENCODE_PARAM;
        }
        return proto_event_from_spi_request(request, buffer, total, 0, 0) ? PROTO_PROC_OK :
               PROTO_PROC_ENCODE_PARAM;
    }
    }
    return PROTO_PROC_ENCODE_PARAM;
}

proto_proc_result_t proto_route_process(const proto_route_t *route, const proto_event_t *source,
                                        proto_event_t *request)
{
    proto_datum_t datum;
    proto_proc_result_t result = proto_route_decode(route, source, &datum);
    if (result != PROTO_PROC_OK) { return result; }
    result = proto_route_map(route, &datum);
    if (result != PROTO_PROC_OK) { return result; }
    result = proto_route_encode(route, &datum, request);
    if (result != PROTO_PROC_OK) { return result; }
    request->device_id = route->destination_device;
    request->timestamp_us = source->timestamp_us;
    proto_event_mark_forwarded(request, source, route->id);
    return PROTO_PROC_OK;
}

/* ---- text ----------------------------------------------------------------------------- */
size_t proto_route_format(const proto_route_t *route, char *out, size_t out_size)
{
    static const char *const dec[] = { "raw", "hex", "ps_can" };
    static const char *const map[] = { "pass", "slice", "scale" };
    static const char *const enc[] = { "raw", "hex", "can_line", "can", "i2c", "spi" };
    static const char *const term[] = { "none", "lf", "crlf" };
    if (!out || out_size == 0) { return 0; }
    out[0] = '\0';
    if (!route) { return 0; }
    char name[8];
    proto_route_name(route->id, name, sizeof(name));
    const int n = snprintf(out, out_size,
        "route_id=%s src=%s dst=%s enabled=%s config=%s dec=%s map=%s enc=%s term=%s policy=%s"
        " hold_ms=%u src_dev=%u dst_dev=%u",
        name, proto_protocol_name(route->source), proto_protocol_name(route->destination),
        route->enabled ? "yes" : "no", proto_route_check_name(proto_route_validate(route)),
        dec[(unsigned)route->decoder <= PROTO_DEC_PS_CAN_V1 ? route->decoder : 0],
        map[(unsigned)route->mapper <= PROTO_MAP_SCALE ? route->mapper : 0],
        enc[(unsigned)route->encoder <= PROTO_ENC_SPI_XFER ? route->encoder : 0],
        term[(unsigned)route->terminator <= PROTO_TERM_CRLF ? route->terminator : 0],
        route->policy == PROTO_POLICY_HOLD ? "hold" : "drop", (unsigned)route->hold_timeout_ms,
        (unsigned)route->source_device, (unsigned)route->destination_device);
    if (n < 0) { out[0] = '\0'; return 0; }
    return (size_t)n < out_size ? (size_t)n : out_size - 1;
}
