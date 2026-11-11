#include "proto_uart_framer.h"

#include <string.h>

bool proto_uart_framer_expire(proto_uart_framer_t *framer, uint64_t now_us)
{
    if ((framer->length == 0 && !framer->discarding) ||
        now_us - framer->last_byte_us < PROTO_UART_FRAME_TIMEOUT_US) {
        return false;
    }
    /* A rejected line is already counted; only a new partial line times out. */
    const bool timed_out = !framer->discarding;
    framer->length = 0;
    framer->discarding = false;
    return timed_out;
}

void proto_uart_framer_resync(proto_uart_framer_t *framer, uint64_t now_us)
{
    framer->length = 0;
    framer->discarding = true;
    framer->last_byte_us = now_us;
}

proto_uart_frame_result_t proto_uart_framer_feed(proto_uart_framer_t *framer,
                                                uint8_t byte, uint64_t now_us,
                                                proto_uart_frame_t *frame)
{
    framer->last_byte_us = now_us;
    if (framer->discarding) {
        if (byte == '\n') {
            framer->discarding = false;
        }
        return PROTO_UART_FRAME_PENDING;
    }

    if (byte == '\n') {
        size_t payload_length = framer->length;
        if (payload_length && framer->data[payload_length - 1] == '\r') {
            --payload_length;
        }
        if (payload_length == 0) {
            framer->length = 0;
            return PROTO_UART_FRAME_MALFORMED;
        }
        framer->data[framer->length++] = byte;
        memcpy(frame->data, framer->data, framer->length);
        frame->length = framer->length;
        frame->completed_us = now_us;
        framer->length = 0;
        return PROTO_UART_FRAME_READY;
    }

    if ((byte < 0x20 && byte != '\r') || byte > 0x7e ||
        (framer->length && framer->data[framer->length - 1] == '\r')) {
        proto_uart_framer_resync(framer, now_us);
        return PROTO_UART_FRAME_MALFORMED;
    }
    /* Reserve one byte for the required LF; never emit a truncated line. */
    if (framer->length == PROTO_UART_FRAME_MAX - 1) {
        proto_uart_framer_resync(framer, now_us);
        return PROTO_UART_FRAME_TOO_LONG;
    }
    framer->data[framer->length++] = byte;
    return PROTO_UART_FRAME_PENDING;
}
