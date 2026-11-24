#pragma once

/* microSD logging runtime (Phase 8): mounts the card on the UI SPI bus, taps the
 * common event contract and writes the Phase 8 CSV schema without ever blocking a
 * communication task. USB log export is not a feature: remove the card and read
 * the LOGnnnn.CSV files on a computer. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_logger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    proto_log_state_t state;
    bool wanted;
    bool card_mounted;
    bool monitor_mode;
    uint32_t protocol_mask;
    char file_name[PROTO_LOG_NAME_MAX];
    uint32_t file_bytes;
    uint32_t records_written;
    uint32_t files_created;
    uint32_t write_errors;
    uint32_t dropped_total;          /* every record that did not reach the file */
    uint32_t dropped_observer;       /* hand-off queue full */
    uint16_t queue_depth;
    uint16_t queue_high_water;
} proto_storage_snapshot_t;

/* Creates the logger task and installs the event observer. Idempotent. Does not
 * touch the card until logging is started. */
esp_err_t proto_storage_start(void);
/* Request logging. Returns the resulting state (NO_CARD, FULL, ERROR are retried in the background). */
proto_log_state_t proto_storage_log_start(void);
void proto_storage_log_stop(void);
void proto_storage_set_protocol_mask(uint32_t mask);
/* Monitor mode: also drain UART/RS-485/CAN events that no enabled route uses so they
 * are logged. This takes those events away from console 'read' commands. */
void proto_storage_set_monitor_mode(bool enabled);
proto_storage_snapshot_t proto_storage_get_snapshot(void);

#ifdef __cplusplus
}
#endif
