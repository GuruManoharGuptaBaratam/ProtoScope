#include "proto_log_queue.h"

#include <string.h>

void proto_log_queue_init(proto_log_queue_t *q, proto_event_t *slots, uint16_t capacity)
{
    if (!q) { return; }
    memset(q, 0, sizeof(*q));
    q->slots = slots;
    q->capacity = capacity;
}

bool proto_log_queue_push(proto_log_queue_t *q, const proto_event_t *event)
{
    if (!q || !q->slots || q->capacity == 0 || !event) { return false; }
    if (q->count >= q->capacity) {
        q->dropped++;
        return false;
    }
    q->slots[(q->head + q->count) % q->capacity] = *event;
    q->count++;
    if (q->count > q->high_water) { q->high_water = q->count; }
    return true;
}

bool proto_log_queue_pop(proto_log_queue_t *q, proto_event_t *event)
{
    if (!q || !q->slots || !event || q->count == 0) { return false; }
    *event = q->slots[q->head];
    q->head = (uint16_t)((q->head + 1u) % q->capacity);
    q->count--;
    return true;
}
