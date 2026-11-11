#pragma once

#include "driver/uart.h"
#include "esp_err.h"
#include "proto_uart_framer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
} proto_uart_config_t;

#define PROTO_UART_CONFIG_DEFAULT() { \
    .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, \
    .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1 }

typedef struct {
    bool active;
    int baud_rate;
    uint32_t rx_bytes;
    uint32_t rx_frames;
    uint32_t tx_bytes;
    uint32_t tx_completed;
    uint32_t malformed;
    uint32_t overlong;
    uint32_t partial_timeouts;
    uint32_t rx_queue_drops;
    uint32_t fifo_overflows;
    uint32_t buffer_full;
    uint32_t frame_errors;
    uint32_t parity_errors;
    uint32_t breaks;
    uint32_t tx_failures;
} proto_uart_snapshot_t;

/* Call start once from the console owner. Reboot to change configuration.
 * Phase 2 supports 9600/115200 baud, 8N1 only; other settings are rejected. */
esp_err_t proto_uart_start(const proto_uart_config_t *config);
/* Bounded raw TX, 1..64 bytes; completion means wire drained, not peer ACK.
 * On timeout a prefix may have left the pin; callers must not blindly retry. */
esp_err_t proto_uart_send(const uint8_t *data, size_t length);
/* Nonblocking receive from the 16-frame application queue. */
esp_err_t proto_uart_receive(proto_uart_frame_t *frame);
proto_uart_snapshot_t proto_uart_get_snapshot(void);

#ifdef __cplusplus
}
#endif
