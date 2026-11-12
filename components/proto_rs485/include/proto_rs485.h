#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_rs485_timing.h"
#include "proto_uart_framer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RS-485 half-duplex serial on UART2 (GPIO15 TX, GPIO16 RX) through an
 * automatic-direction transceiver module. There is no DE/RE pin: the module
 * switches direction itself and the firmware adds NO direction control.
 * Phase 4 is raw serial only (LF-terminated ASCII lines, same framing as the
 * Phase 2 UART). Modbus RTU is NOT implemented here. */
typedef struct {
    uint32_t baud_rate;   /* 9600, 19200, 38400, 57600 or 115200; 8N1 */
    bool discard_echo;    /* drop bytes received during our own TX + guard window */
} proto_rs485_config_t;

#define PROTO_RS485_CONFIG_DEFAULT() { .baud_rate = 9600, .discard_echo = true }

typedef struct {
    bool active;
    uint32_t baud_rate;
    bool discard_echo;
    uint32_t guard_us;
    uint32_t rx_bytes;         /* bytes delivered to the framer */
    uint32_t rx_frames;
    uint32_t tx_bytes;
    uint32_t tx_completed;     /* wire drained; NOT a peer acknowledgement */
    uint32_t echo_bytes;       /* bytes discarded inside our own TX + guard window */
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
} proto_rs485_snapshot_t;

/* Start once from the console owner; reboot to change settings. */
esp_err_t proto_rs485_start(const proto_rs485_config_t *config);
/* Bounded raw TX, 1..64 bytes. Completion means the UART drained, not that a
 * peer received it. Anything received while transmitting (and for a short
 * guard time afterwards) is treated as echo or collision, counted in
 * echo_bytes and discarded when discard_echo is true. */
esp_err_t proto_rs485_send(const uint8_t *data, size_t length);
/* Nonblocking receive from the 16-frame application queue. */
esp_err_t proto_rs485_receive(proto_uart_frame_t *frame);
proto_rs485_snapshot_t proto_rs485_get_snapshot(void);

#ifdef __cplusplus
}
#endif
