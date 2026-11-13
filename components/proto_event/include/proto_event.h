#pragma once

/* Common event envelope with protocol-specific metadata (Phase 6).
 *
 * Pure C: depends only on the pure headers of the interface components, so it
 * is host-tested. The envelope never reduces an interface to bytes only: each
 * protocol keeps the metadata routing, decoding, monitoring and logging need.
 *
 * Ownership: an event OWNS a fixed inline copy of its payload (no pointers). It
 * can be passed or queued by value, has no lifetime coupling to a driver buffer,
 * and is safe to hand to another task. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proto_can_codec.h"
#include "proto_i2c_codec.h"
#include "proto_spi_codec.h"
#include "proto_uart_framer.h"

#define PROTO_EVENT_MAX_PAYLOAD 64
#define PROTO_EVENT_MAX_HOPS 4 /* loop guard: Phase 7 rejects events at/above this */

typedef enum {
    PROTO_PROTOCOL_UART = 0,
    PROTO_PROTOCOL_RS485,
    PROTO_PROTOCOL_CAN,
    PROTO_PROTOCOL_I2C,
    PROTO_PROTOCOL_SPI,
    PROTO_PROTOCOL_COUNT,
} proto_protocol_t;

typedef enum { PROTO_DIR_RX = 0, PROTO_DIR_TX } proto_direction_t;

typedef enum {
    PROTO_EVENT_STATUS_OK = 0,
    PROTO_EVENT_STATUS_TIMEOUT,
    PROTO_EVENT_STATUS_NACK,      /* I2C device did not acknowledge */
    PROTO_EVENT_STATUS_BUS_ERROR,
    PROTO_EVENT_STATUS_INVALID,   /* malformed / failed validation */
    PROTO_EVENT_STATUS_REJECTED,  /* refused before reaching the interface */
} proto_event_status_t;

typedef enum { PROTO_TERM_NONE = 0, PROTO_TERM_LF, PROTO_TERM_CRLF } proto_serial_terminator_t;

/* UART and RS-485: a framed byte stream. baud_rate 0 on a TX request means
 * "the driver's configured rate"; the terminator is kept so CR/LF is not lost. */
typedef struct {
    uint32_t baud_rate;
    proto_serial_terminator_t terminator;
} proto_serial_meta_t;

typedef struct {
    uint32_t id;
    bool extended;
    bool rtr;
    uint8_t dlc;
} proto_can_meta_t;

typedef enum { PROTO_I2C_ACK_UNKNOWN = 0, PROTO_I2C_ACK_ACK, PROTO_I2C_ACK_NACK } proto_i2c_ack_t;

/* Payload layout: a TX request carries the write bytes (write_length of them,
 * the leading register/command bytes included); an RX result carries the bytes
 * that were read (read_length of them). */
typedef struct {
    uint8_t address;               /* 7-bit */
    proto_i2c_op_t op;
    uint8_t write_length;
    uint8_t read_length;
    proto_i2c_ack_t ack;
} proto_i2c_meta_t;

/* Full-duplex: a TX request carries MOSI bytes; the RX result carries MISO bytes
 * of the same length. clock_hz 0 on a request means "the configured clock". */
typedef struct {
    uint8_t chip_select;
    uint8_t mode;
    uint32_t clock_hz;
    uint8_t word_bits;             /* always 8 in Phase 5/6 */
    uint16_t transaction_length;
} proto_spi_meta_t;

typedef struct {
    uint64_t event_id;             /* unique, assigned by the driver adapter */
    uint64_t correlation_id;       /* request event_id this result answers; 0 = none */
    proto_protocol_t protocol;     /* source/destination interface; also the meta tag */
    uint8_t device_id;             /* configured device identifier; 0 = default */
    proto_direction_t direction;
    proto_event_status_t status;
    int32_t error_code;            /* driver error (esp_err_t value); 0 = none */
    uint64_t timestamp_us;         /* boot-relative monotonic time */
    uint8_t hop_count;             /* 0 for external traffic; +1 per forward */
    uint8_t origin_route_id;       /* route that created it; 0 = external */
    bool from_protoscope;          /* ProtoScope transmitted it: never a fresh source */
    uint16_t payload_length;
    uint8_t payload[PROTO_EVENT_MAX_PAYLOAD];
    union {
        proto_serial_meta_t serial;
        proto_can_meta_t can;
        proto_i2c_meta_t i2c;
        proto_spi_meta_t spi;
    } meta;
} proto_event_t;

typedef enum {
    PROTO_EVENT_OK = 0,
    PROTO_EVENT_ERR_NULL,
    PROTO_EVENT_ERR_PROTOCOL,
    PROTO_EVENT_ERR_DIRECTION,
    PROTO_EVENT_ERR_STATUS,
    PROTO_EVENT_ERR_LENGTH,
    PROTO_EVENT_ERR_METADATA,
    PROTO_EVENT_ERR_HOPS,
} proto_event_check_t;

/* Zeroes the event and sets protocol and direction. */
void proto_event_init(proto_event_t *event, proto_protocol_t protocol, proto_direction_t direction);
proto_event_check_t proto_event_validate(const proto_event_t *event);
const char *proto_event_check_name(proto_event_check_t check);
const char *proto_event_status_name(proto_event_status_t status);

/* --- Build events from / convert events to the interface types. ------------ */
bool proto_event_from_can(proto_event_t *event, const proto_can_frame_t *frame,
                          proto_direction_t direction);
bool proto_event_to_can(const proto_event_t *event, proto_can_frame_t *frame);

/* UART or RS-485 frame (the framed line keeps every wire byte). */
bool proto_event_from_serial(proto_event_t *event, proto_protocol_t protocol,
                             const proto_uart_frame_t *frame, uint32_t baud_rate,
                             proto_direction_t direction);
bool proto_event_to_serial(const proto_event_t *event, uint8_t *out, size_t out_size,
                           size_t *length);

/* I2C: request -> TX event; result event answers a request. */
bool proto_event_from_i2c_request(proto_event_t *event, const proto_i2c_txn_t *txn);
bool proto_event_to_i2c(const proto_event_t *event, proto_i2c_txn_t *txn);
bool proto_event_i2c_result(proto_event_t *result, const proto_event_t *request,
                            const uint8_t *read_data, proto_event_status_t status,
                            int32_t error_code, uint64_t timestamp_us);

/* SPI: request -> TX event (MOSI); result event carries MISO. */
bool proto_event_from_spi_request(proto_event_t *event, const uint8_t *tx, size_t length,
                                  uint8_t mode, uint32_t clock_hz);
bool proto_event_to_spi(const proto_event_t *event, uint8_t *tx, size_t tx_size, size_t *length);
bool proto_event_spi_result(proto_event_t *result, const proto_event_t *request,
                            const uint8_t *rx_data, uint8_t mode, uint32_t clock_hz,
                            proto_event_status_t status, int32_t error_code,
                            uint64_t timestamp_us);

/* --- Route provenance / loop prevention. ----------------------------------- */
/* Derive the event ProtoScope will transmit for a route from a source event:
 * hop_count + 1, from_protoscope = true, origin_route_id = route_id. */
void proto_event_mark_forwarded(proto_event_t *out, const proto_event_t *source, uint8_t route_id);
/* True when the event must be refused as a route source (ProtoScope's own
 * transmission, or too many hops). */
bool proto_event_loop_rejected(const proto_event_t *event);

/* One-line text form for the console and logs. Returns the length. */
size_t proto_event_format(const proto_event_t *event, char *out, size_t out_size);
