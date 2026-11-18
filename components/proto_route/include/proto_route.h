#pragma once

/* Phase 7: configurable many-to-many routing and conversion engine.
 *
 * Pure C (no ESP-IDF dependency) so the whole engine is host-tested with mock
 * interfaces; proto_route_runtime.h binds it to the real drivers.
 *
 * Pipeline per route:  receive -> match -> decode -> map -> encode -> transmit -> count.
 * A route is not a wire: each of the 20 directed routes RT-01..RT-20 (route-matrix.md)
 * has its own source/destination mode, decoder, mapping, encoder, validation and
 * counters. Device-specific details stay TBD (route stays "not configured" and
 * cannot be enabled) until they are set; nothing here invents a register map,
 * CAN identifier meaning or peripheral command. Raw forwarding (decoder RAW,
 * mapper PASS) is kept distinct from semantic mapping (SCALE, PS_CAN_V1). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proto_event.h"
#include "proto_event_caps.h"

#define PROTO_ROUTE_COUNT 20            /* RT-01 .. RT-20 */
#define PROTO_ROUTE_PREFIX_MAX 8
#define PROTO_ROUTE_FINGERPRINTS 16
#define PROTO_ROUTE_RX_BUDGET 16        /* events pulled per interface per step */
#define PROTO_ROUTE_DEFAULT_HOLD_MS 200
#define PROTO_ROUTE_DEFAULT_LOOP_WINDOW_MS 100

/* Intermediate value moved between decode, map and encode. */
typedef struct {
    uint8_t data[PROTO_EVENT_MAX_PAYLOAD];
    uint16_t length;
    bool has_can_id;       /* set when the source was CAN or the PS-UART-CAN/1 grammar */
    uint32_t can_id;
    bool can_extended;
    bool can_rtr;
} proto_datum_t;

typedef enum {
    PROTO_DEC_RAW = 0,     /* payload as is (serial: terminator removed) */
    PROTO_DEC_HEX_LINE,    /* serial line of ASCII hex digits -> bytes */
    PROTO_DEC_PS_CAN_V1,   /* serial "CAN,<id_hex>,<dlc>,<payload_hex>" (RT-01 grammar) */
} proto_decoder_t;

typedef enum { PROTO_MAP_PASS = 0, PROTO_MAP_SLICE, PROTO_MAP_SCALE } proto_mapper_t;

typedef enum {
    PROTO_ENC_SERIAL_RAW = 0,   /* datum bytes + terminator */
    PROTO_ENC_SERIAL_HEX,       /* datum as ASCII hex + terminator */
    PROTO_ENC_SERIAL_CAN_LINE,  /* "CAN,<id_hex>,<dlc>,<payload_hex>" + terminator */
    PROTO_ENC_CAN,              /* CAN data frame */
    PROTO_ENC_I2C_WRITE,        /* controller write: prefix + datum */
    PROTO_ENC_SPI_XFER,         /* controller transfer: prefix + datum on MOSI */
} proto_encoder_t;

typedef enum { PROTO_POLICY_DROP = 0, PROTO_POLICY_HOLD } proto_policy_t;

/* Integer scaling: v' = (v * mul) / div + add, widths 1/2/4 bytes, either
 * endianness, signed or unsigned, with range checking (out-of-range is rejected). */
typedef struct {
    uint8_t in_width;
    bool in_big_endian;
    bool in_signed;
    int32_t mul;
    int32_t div;
    int32_t add;
    uint8_t out_width;
    bool out_big_endian;
    bool out_signed;
} proto_scale_t;

typedef struct {
    uint8_t id;                         /* 1..20 = RT-01..RT-20 */
    bool enabled;
    proto_protocol_t source;
    proto_protocol_t destination;
    uint8_t source_device;              /* configured device ids (0 = default) */
    uint8_t destination_device;

    bool has_filter;                    /* CAN source: (id & mask) == (filter_id & mask) */
    uint32_t filter_id;
    uint32_t filter_mask;
    bool filter_extended;

    bool poll_configured;               /* I2C / SPI source: scheduled controller read */
    proto_i2c_txn_t poll_i2c;
    uint8_t poll_spi[PROTO_SPI_MAX_TRANSFER];
    uint8_t poll_spi_length;
    uint32_t poll_interval_ms;          /* 0 = only on 'route trigger' */

    proto_decoder_t decoder;
    proto_mapper_t mapper;
    uint8_t slice_offset;
    uint8_t slice_length;
    proto_scale_t scale;

    proto_encoder_t encoder;
    proto_serial_terminator_t terminator;
    bool can_id_from_datum;
    bool can_id_set;
    uint32_t can_id;
    bool can_extended;
    uint8_t i2c_address;                /* 0 = not set */
    bool prefix_set;                    /* I2C/SPI destination: command bytes explicitly chosen (prefix=none counts) */
    uint8_t prefix[PROTO_ROUTE_PREFIX_MAX];
    uint8_t prefix_length;

    proto_policy_t policy;              /* destination busy: DROP (count) or HOLD (retry) */
    uint32_t hold_timeout_ms;
} proto_route_t;

typedef enum {
    PROTO_ROUTE_OK = 0,
    PROTO_ROUTE_ERR_ID,
    PROTO_ROUTE_ERR_SAME_INTERFACE,
    PROTO_ROUTE_ERR_FILTER_MISSING,
    PROTO_ROUTE_ERR_POLL_MISSING,
    PROTO_ROUTE_ERR_DECODER,
    PROTO_ROUTE_ERR_MAPPER,
    PROTO_ROUTE_ERR_ENCODER,
    PROTO_ROUTE_ERR_DEST_PARAM,
    PROTO_ROUTE_ERR_POLICY,
} proto_route_check_t;

typedef enum {
    PROTO_PROC_OK = 0,
    PROTO_PROC_DECODE_FORMAT,
    PROTO_PROC_DECODE_ID,
    PROTO_PROC_DECODE_DLC,
    PROTO_PROC_DECODE_PAYLOAD,
    PROTO_PROC_DECODE_LENGTH,
    PROTO_PROC_MAP_LENGTH,
    PROTO_PROC_MAP_RANGE,
    PROTO_PROC_ENCODE_LENGTH,
    PROTO_PROC_ENCODE_PARAM,
    PROTO_PROC_ENCODE_UNREPRESENTABLE,
} proto_proc_result_t;

typedef enum { PROTO_SET_OK = 0, PROTO_SET_UNKNOWN_KEY, PROTO_SET_BAD_VALUE } proto_route_set_result_t;

/* ---- configuration ------------------------------------------------------------- */
/* Structural default profile for RT-01..RT-20 (disabled, device details TBD). */
void proto_route_defaults(proto_route_t *route, uint8_t id);
/* Why a route cannot be enabled (PROTO_ROUTE_OK when it is fully configured). */
proto_route_check_t proto_route_validate(const proto_route_t *route);
const char *proto_route_check_name(proto_route_check_t check);
const char *proto_proc_result_name(proto_proc_result_t result);
/* key=value setter used by the console: see docs/phase-7-routing-engine.md. */
proto_route_set_result_t proto_route_set(proto_route_t *route, const char *key, const char *value);
/* "RT-01" into out (>= 6 bytes). */
void proto_route_name(uint8_t id, char *out, size_t out_size);
size_t proto_route_format(const proto_route_t *route, char *out, size_t out_size);

/* ---- pipeline stages (pure) ------------------------------------------------------- */
bool proto_route_match(const proto_route_t *route, const proto_event_t *source);
proto_proc_result_t proto_route_decode(const proto_route_t *route, const proto_event_t *source,
                                       proto_datum_t *datum);
proto_proc_result_t proto_route_map(const proto_route_t *route, proto_datum_t *datum);
proto_proc_result_t proto_route_encode(const proto_route_t *route, const proto_datum_t *datum,
                                       proto_event_t *request);
/* decode + map + encode; on success `request` is a TX event for the destination
 * with provenance (hop + 1, origin route, correlation to the source event). */
proto_proc_result_t proto_route_process(const proto_route_t *route, const proto_event_t *source,
                                        proto_event_t *request);

/* ---- engine ----------------------------------------------------------------------- */
typedef struct {
    uint32_t matched;          /* events that passed the route's source filter */
    uint32_t filtered;         /* events the filter rejected */
    uint32_t transmitted;      /* destination driver accepted the request */
    uint32_t rejected_decode;
    uint32_t rejected_map;
    uint32_t rejected_encode;
    uint32_t dest_unavailable; /* destination interface not started */
    uint32_t dest_failures;
    uint32_t dropped_busy;     /* destination busy under the DROP policy */
    uint32_t timed_out;        /* held request expired under the HOLD policy */
    uint32_t polls;            /* scheduled source reads attempted */
    uint32_t poll_failures;
    int32_t last_error;
    proto_proc_result_t last_reject;
} proto_route_stats_t;

typedef struct {
    uint32_t rx_events;        /* events pulled from this interface by the engine */
    uint32_t loop_suppressed;  /* matched ProtoScope's own recent transmission */
    uint32_t loop_rejected;    /* provenance/hop-count guard */
    uint32_t tx_submitted;
} proto_iface_stats_t;

typedef enum { PROTO_IO_OK = 0, PROTO_IO_EMPTY, PROTO_IO_BUSY, PROTO_IO_FAILED } proto_io_status_t;

/* The engine only talks to interfaces through this table (real drivers in the
 * runtime, mocks in the host tests). */
typedef struct {
    void *context;
    bool (*is_active)(void *context, proto_protocol_t protocol);
    proto_io_status_t (*receive)(void *context, proto_protocol_t protocol, proto_event_t *event);
    proto_io_status_t (*submit)(void *context, proto_protocol_t protocol,
                                const proto_event_t *request, proto_event_t *result,
                                int32_t *error);
    uint64_t (*now_us)(void *context);
    uint64_t (*next_id)(void *context);
} proto_route_io_t;

typedef struct {
    proto_route_t config;
    proto_route_stats_t stats;
    bool pending;                      /* HOLD policy: request waiting for the destination */
    proto_event_t pending_request;
    uint64_t pending_since_us;
    uint64_t next_poll_us;
    bool triggered;
} proto_route_slot_t;

typedef struct {
    uint8_t protocol;
    uint32_t hash;
    uint64_t time_us;
    bool valid;
} proto_fingerprint_t;

typedef struct {
    proto_route_slot_t routes[PROTO_ROUTE_COUNT];
    proto_iface_stats_t iface[PROTO_PROTOCOL_COUNT];
    proto_fingerprint_t fingerprints[PROTO_ROUTE_FINGERPRINTS];
    uint8_t fingerprint_next;
    uint32_t loop_window_ms;           /* 0 disables echo suppression */
    uint32_t steps;
} proto_route_engine_t;

/* Loads the 20 default profiles, all disabled. */
void proto_route_engine_init(proto_route_engine_t *engine);
proto_route_t *proto_route_engine_route(proto_route_engine_t *engine, uint8_t id);
const proto_route_stats_t *proto_route_engine_stats(const proto_route_engine_t *engine, uint8_t id);
/* Rejects (and stays disabled) unless the route validates; never throws. */
proto_route_check_t proto_route_engine_enable(proto_route_engine_t *engine, uint8_t id);
void proto_route_engine_disable(proto_route_engine_t *engine, uint8_t id);
/* One scheduled source read on the next step (I2C/SPI sources only). */
bool proto_route_engine_trigger(proto_route_engine_t *engine, uint8_t id);
void proto_route_engine_reset_stats(proto_route_engine_t *engine);
/* One non-blocking pass: retry held requests, run due polls, pull and route events. */
void proto_route_engine_step(proto_route_engine_t *engine, const proto_route_io_t *io);
