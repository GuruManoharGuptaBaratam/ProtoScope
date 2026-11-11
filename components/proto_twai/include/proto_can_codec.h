#pragma once

/* Pure-C CAN frame helpers (no ESP-IDF dependency) so they can be host-tested. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_CAN_MAX_DLC 8
#define PROTO_CAN_STD_ID_MAX 0x7FFu
#define PROTO_CAN_EXT_ID_MAX 0x1FFFFFFFu
/* "XXXXXXXX#" + 16 hex digits + NUL = 26; keep a little headroom. */
#define PROTO_CAN_TEXT_MAX 32

typedef struct {
    uint32_t id;
    bool extended;
    bool rtr;
    uint8_t dlc;
    uint8_t data[PROTO_CAN_MAX_DLC];
    uint64_t timestamp_us;
} proto_can_frame_t;

typedef enum {
    PROTO_CAN_PARSE_OK = 0,
    PROTO_CAN_PARSE_BAD_FORMAT,
    PROTO_CAN_PARSE_BAD_ID,
    PROTO_CAN_PARSE_BAD_DATA,
} proto_can_parse_result_t;

/* Phase 3 supports 125 k, 250 k, 500 k and 1 M bit/s. */
bool proto_can_bitrate_supported(uint32_t bit_rate);

/* Text form (candump/cansend style): "123#0105FF" = 11-bit ID, "1F334455#11" =
 * 29-bit ID (exactly 3 or 8 hex digits), "123#" = empty data frame,
 * "123#R" = remote request (DLC 0). Up to 8 data bytes. */
proto_can_parse_result_t proto_can_parse_frame(const char *text, proto_can_frame_t *frame);

/* Writes the same text form. Returns the length, or 0 if the frame is invalid
 * or out_size is too small. */
size_t proto_can_format_frame(const proto_can_frame_t *frame, char *out, size_t out_size);

/* True when the ID fits the frame type, DLC <= 8 and RTR frames carry no data. */
bool proto_can_frame_valid(const proto_can_frame_t *frame);
