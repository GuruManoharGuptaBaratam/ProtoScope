#pragma once

/* Bounded FIFO of events between communication processing and SD writes. Pure C,
 * not thread-safe: the runtime wraps push/pop in a lock. A full queue drops the
 * NEWEST event and counts it, so overflow is always visible. */

#include <stdbool.h>
#include <stdint.h>

#include "proto_event.h"

typedef struct {
    proto_event_t *slots;
    uint16_t capacity;
    uint16_t head;
    uint16_t count;
    uint32_t dropped;
    uint16_t high_water;
} proto_log_queue_t;

void proto_log_queue_init(proto_log_queue_t *queue, proto_event_t *slots, uint16_t capacity);
bool proto_log_queue_push(proto_log_queue_t *queue, const proto_event_t *event);
bool proto_log_queue_pop(proto_log_queue_t *queue, proto_event_t *event);
