#pragma once

/* Logger state machine (Phase 8). Pure C over an abstract file sink, so queueing,
 * rotation, retry and every storage failure mode are host-tested with a mock card.
 *
 * States: OFF (not requested) -> RUNNING; NO_CARD (card missing/unreadable), FULL
 * (no space or file numbers used up), ERROR (write/flush failure). While not RUNNING
 * every submitted event is dropped and COUNTED; the logger retries in the background
 * (5 s for NO_CARD/ERROR, 30 s for FULL), opening a NEW file each time. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proto_log_format.h"
#include "proto_log_queue.h"

#define PROTO_LOG_OK 0
#define PROTO_LOG_ERR_NO_CARD (-1)
#define PROTO_LOG_ERR_FULL (-2)
#define PROTO_LOG_ERR_IO (-3)

#define PROTO_LOG_DEFAULT_MAX_FILE_BYTES (4u * 1024u * 1024u)
#define PROTO_LOG_FLUSH_RECORDS 32u
#define PROTO_LOG_FLUSH_INTERVAL_US 1000000ull
#define PROTO_LOG_RETRY_US 5000000ull
#define PROTO_LOG_RETRY_FULL_US 30000000ull
#define PROTO_LOG_SERVICE_BATCH 64u

typedef struct {
    void *context;
    /* Highest existing LOGnnnn.CSV index (0 if none). Returns PROTO_LOG_OK or an error. */
    int (*list_max_index)(void *context, uint16_t *max_index);
    int (*open)(void *context, const char *name);                  /* create/truncate */
    int (*write)(void *context, const char *data, size_t length);
    int (*flush)(void *context);
    void (*close)(void *context);
} proto_log_sink_t;

typedef enum {
    PROTO_LOG_STATE_OFF = 0,
    PROTO_LOG_STATE_RUNNING,
    PROTO_LOG_STATE_NO_CARD,
    PROTO_LOG_STATE_FULL,
    PROTO_LOG_STATE_ERROR,
} proto_log_state_t;

typedef struct {
    proto_log_sink_t sink;
    const char *firmware;
    proto_log_state_t state;
    bool wanted;
    uint32_t protocol_mask;
    uint32_t max_file_bytes;
    uint16_t file_index;
    char file_name[PROTO_LOG_NAME_MAX];
    uint32_t file_bytes;
    proto_log_queue_t queue;
    uint32_t records_written;
    uint32_t dropped_not_running;   /* submitted while NO_CARD / FULL / ERROR */
    uint32_t dropped_write_failed;  /* record lost because a write failed */
    uint32_t write_errors;
    uint32_t files_created;
    uint32_t flushes;
    uint32_t unflushed;
    uint64_t last_flush_us;
    uint64_t state_since_us;
} proto_logger_t;

void proto_logger_init(proto_logger_t *logger, const proto_log_sink_t *sink, const char *firmware,
                       proto_event_t *slots, uint16_t capacity);
/* Request logging and try to open the next numbered file now. True when RUNNING. */
bool proto_logger_start(proto_logger_t *logger, uint64_t now_us);
/* Write out queued events, flush and close; stays OFF until started again. */
void proto_logger_stop(proto_logger_t *logger, uint64_t now_us);
/* Filter by protocol mask, then queue. False when the event was not accepted. */
bool proto_logger_submit(proto_logger_t *logger, const proto_event_t *event);
/* Drain queue to the file, rotate, flush, retry after failures. Call often. */
void proto_logger_service(proto_logger_t *logger, uint64_t now_us);
uint32_t proto_logger_dropped_total(const proto_logger_t *logger);
const char *proto_logger_state_name(proto_log_state_t state);
