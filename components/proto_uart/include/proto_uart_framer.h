#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_UART_FRAME_MAX 64
#define PROTO_UART_FRAME_TIMEOUT_US 200000ULL

typedef struct {
    uint8_t data[PROTO_UART_FRAME_MAX];
    size_t length;
    uint64_t completed_us;
} proto_uart_frame_t;

typedef struct {
    uint8_t data[PROTO_UART_FRAME_MAX];
    size_t length;
    uint64_t last_byte_us;
    bool discarding;
} proto_uart_framer_t;

typedef enum {
    PROTO_UART_FRAME_PENDING,
    PROTO_UART_FRAME_READY,
    PROTO_UART_FRAME_MALFORMED,
    PROTO_UART_FRAME_TOO_LONG,
} proto_uart_frame_result_t;

/* The caller must expire before feeding each chunk, including after idle gaps.
 * Accepted frames preserve every wire byte, including CR/LF. */
bool proto_uart_framer_expire(proto_uart_framer_t *framer, uint64_t now_us);
proto_uart_frame_result_t proto_uart_framer_feed(proto_uart_framer_t *framer,
                                                uint8_t byte, uint64_t now_us,
                                                proto_uart_frame_t *frame);
/* After data loss, discard through the next LF or idle boundary. */
void proto_uart_framer_resync(proto_uart_framer_t *framer, uint64_t now_us);
