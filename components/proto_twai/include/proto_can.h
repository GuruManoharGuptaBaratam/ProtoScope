#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_can_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROTO_CAN_MODE_NORMAL = 0,   /* Normal node: ACKs frames, transmits. */
    PROTO_CAN_MODE_LISTEN_ONLY,  /* Receive only; never drives the bus (no ACK). */
    PROTO_CAN_MODE_SELF_TEST,    /* No-ACK + self-reception: single-node loopback check. */
} proto_can_mode_t;

typedef struct {
    uint32_t bit_rate; /* 125000, 250000, 500000 or 1000000 */
    proto_can_mode_t mode;
} proto_can_config_t;

#define PROTO_CAN_CONFIG_DEFAULT() { .bit_rate = 500000, .mode = PROTO_CAN_MODE_NORMAL }

typedef enum {
    PROTO_CAN_STATE_STOPPED = 0,
    PROTO_CAN_STATE_RUNNING,
    PROTO_CAN_STATE_BUS_OFF,
    PROTO_CAN_STATE_RECOVERING,
} proto_can_state_t;

typedef struct {
    bool active;
    uint32_t bit_rate;
    proto_can_mode_t mode;
    proto_can_state_t state;
    uint32_t tx_error_counter;   /* controller TEC */
    uint32_t rx_error_counter;   /* controller REC */
    uint32_t rx_frames;          /* frames taken from the TWAI RX queue */
    uint32_t tx_requested;       /* frames accepted by proto_can_send */
    uint32_t tx_rejected;        /* proto_can_send calls refused or timed out */
    uint32_t tx_completed;       /* TX-success alerts (frame ACKed on the wire) */
    uint32_t tx_failed;          /* controller-reported failed transmissions */
    uint32_t rx_queue_drops;     /* application queue full */
    uint32_t rx_missed;          /* controller-reported missed frames */
    uint32_t rx_overrun;         /* controller RX FIFO/queue overruns */
    uint32_t arb_lost;
    uint32_t bus_errors;
    uint32_t error_passive;      /* times the node entered error-passive */
    uint32_t bus_off;            /* times the node went bus-off */
    uint32_t recoveries;         /* completed bus-off recoveries */
} proto_can_snapshot_t;

/* Start once from the console owner; reboot to change configuration.
 * Uses the ESP32-S3 TWAI controller on PROTO_PIN_TWAI_TX/RX with an external
 * transceiver. Accept-all filter. Bus-off recovery is automatic. */
esp_err_t proto_can_start(const proto_can_config_t *config);
/* Queue one frame (data or remote) without blocking: a full TX queue returns
 * ESP_ERR_TIMEOUT and is counted in tx_rejected. Completion is reported asynchronously by
 * tx_completed; with no other ACKing node on the bus a frame is retried and
 * the error counters climb. Not available in listen-only mode. */
esp_err_t proto_can_send(const proto_can_frame_t *frame);
/* Nonblocking receive from the 16-frame application queue. */
esp_err_t proto_can_receive(proto_can_frame_t *frame);
proto_can_snapshot_t proto_can_get_snapshot(void);

#ifdef __cplusplus
}
#endif
