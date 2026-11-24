#include "proto_logger.h"

#include <string.h>

const char *proto_logger_state_name(proto_log_state_t state)
{
    switch (state) {
    case PROTO_LOG_STATE_OFF: return "off";
    case PROTO_LOG_STATE_RUNNING: return "running";
    case PROTO_LOG_STATE_NO_CARD: return "no_card";
    case PROTO_LOG_STATE_FULL: return "full";
    case PROTO_LOG_STATE_ERROR: return "error";
    }
    return "?";
}

void proto_logger_init(proto_logger_t *l, const proto_log_sink_t *sink, const char *firmware,
                       proto_event_t *slots, uint16_t capacity)
{
    if (!l || !sink) { return; }
    memset(l, 0, sizeof(*l));
    l->sink = *sink;
    l->firmware = firmware;
    l->protocol_mask = PROTO_LOG_MASK_ALL;
    l->max_file_bytes = PROTO_LOG_DEFAULT_MAX_FILE_BYTES;
    proto_log_queue_init(&l->queue, slots, capacity);
}

uint32_t proto_logger_dropped_total(const proto_logger_t *l)
{
    return l ? l->queue.dropped + l->dropped_not_running + l->dropped_write_failed : 0;
}

static void set_state(proto_logger_t *l, proto_log_state_t state, uint64_t now_us)
{
    l->state = state;
    l->state_since_us = now_us;
}

static void fail(proto_logger_t *l, int code, uint64_t now_us)
{
    if (l->file_name[0]) { l->sink.close(l->sink.context); }
    l->file_name[0] = '\0';
    l->unflushed = 0;
    if (code == PROTO_LOG_ERR_NO_CARD) { set_state(l, PROTO_LOG_STATE_NO_CARD, now_us); }
    else if (code == PROTO_LOG_ERR_FULL) { set_state(l, PROTO_LOG_STATE_FULL, now_us); }
    else { set_state(l, PROTO_LOG_STATE_ERROR, now_us); }
    if (code != PROTO_LOG_ERR_NO_CARD) { l->write_errors++; }
    /* Anything still queued belongs to a file that no longer exists. */
    proto_event_t discard;
    while (proto_log_queue_pop(&l->queue, &discard)) { l->dropped_write_failed++; }
}

/* Opens the next numbered file and writes the header. */
static bool open_next(proto_logger_t *l, uint64_t now_us)
{
    uint16_t max_index = 0;
    int rc = l->sink.list_max_index(l->sink.context, &max_index);
    if (rc != PROTO_LOG_OK) { fail(l, rc, now_us); return false; }
    if (max_index >= PROTO_LOG_INDEX_MAX) { fail(l, PROTO_LOG_ERR_FULL, now_us); return false; }
    l->file_index = (uint16_t)(max_index + 1u);
    proto_log_make_name(l->file_index, l->file_name, sizeof(l->file_name));
    rc = l->sink.open(l->sink.context, l->file_name);
    if (rc != PROTO_LOG_OK) { l->file_name[0] = '\0'; fail(l, rc, now_us); return false; }
    char header[256];
    const size_t n = proto_log_format_header(l->firmware, header, sizeof(header));
    rc = n ? l->sink.write(l->sink.context, header, n) : PROTO_LOG_ERR_IO;
    if (rc != PROTO_LOG_OK) { fail(l, rc, now_us); return false; }
    l->file_bytes = (uint32_t)n;
    l->files_created++;
    l->unflushed = 0;
    l->last_flush_us = now_us;
    set_state(l, PROTO_LOG_STATE_RUNNING, now_us);
    return true;
}

bool proto_logger_start(proto_logger_t *l, uint64_t now_us)
{
    if (!l) { return false; }
    l->wanted = true;
    if (l->state == PROTO_LOG_STATE_RUNNING) { return true; }
    return open_next(l, now_us);
}

static bool flush_now(proto_logger_t *l, uint64_t now_us)
{
    if (l->unflushed == 0) { return true; }
    const int rc = l->sink.flush(l->sink.context);
    if (rc != PROTO_LOG_OK) { fail(l, rc, now_us); return false; }
    l->flushes++;
    l->unflushed = 0;
    l->last_flush_us = now_us;
    return true;
}

static void drain(proto_logger_t *l, uint64_t now_us, unsigned limit)
{
    proto_event_t event;
    char line[PROTO_LOG_LINE_MAX];
    for (unsigned i = 0; i < limit && l->state == PROTO_LOG_STATE_RUNNING; ++i) {
        if (!proto_log_queue_pop(&l->queue, &event)) { break; }
        const size_t n = proto_log_format_record(&event, line, sizeof(line));
        if (n == 0) { l->dropped_write_failed++; continue; }
        if (l->file_bytes + n > l->max_file_bytes) {
            if (!flush_now(l, now_us)) { l->dropped_write_failed++; return; }
            l->sink.close(l->sink.context);
            l->file_name[0] = '\0';
            if (!open_next(l, now_us)) { l->dropped_write_failed++; return; }
        }
        const int rc = l->sink.write(l->sink.context, line, n);
        if (rc != PROTO_LOG_OK) {
            l->dropped_write_failed++;
            fail(l, rc, now_us);
            return;
        }
        l->file_bytes += (uint32_t)n;
        l->records_written++;
        l->unflushed++;
        if (l->unflushed >= PROTO_LOG_FLUSH_RECORDS && !flush_now(l, now_us)) { return; }
    }
}

void proto_logger_stop(proto_logger_t *l, uint64_t now_us)
{
    if (!l) { return; }
    if (l->state == PROTO_LOG_STATE_RUNNING) {
        drain(l, now_us, 0xFFFFu);
        if (l->state == PROTO_LOG_STATE_RUNNING) {
            (void)flush_now(l, now_us);
            l->sink.close(l->sink.context);
            l->file_name[0] = '\0';
        }
    }
    l->wanted = false;
    set_state(l, PROTO_LOG_STATE_OFF, now_us);
}

bool proto_logger_submit(proto_logger_t *l, const proto_event_t *event)
{
    if (!l || !event || (unsigned)event->protocol >= PROTO_PROTOCOL_COUNT) { return false; }
    if (!(l->protocol_mask & PROTO_LOG_MASK(event->protocol))) { return false; }
    if (!l->wanted) { return false; }
    if (l->state != PROTO_LOG_STATE_RUNNING) {
        l->dropped_not_running++;
        return false;
    }
    return proto_log_queue_push(&l->queue, event);
}

void proto_logger_service(proto_logger_t *l, uint64_t now_us)
{
    if (!l || !l->wanted) { return; }
    if (l->state != PROTO_LOG_STATE_RUNNING) {
        const uint64_t wait = l->state == PROTO_LOG_STATE_FULL ? PROTO_LOG_RETRY_FULL_US : PROTO_LOG_RETRY_US;
        if (l->state != PROTO_LOG_STATE_OFF && now_us - l->state_since_us >= wait) {
            (void)open_next(l, now_us);
        }
        return;
    }
    drain(l, now_us, PROTO_LOG_SERVICE_BATCH);
    if (l->state == PROTO_LOG_STATE_RUNNING && l->unflushed > 0 &&
        now_us - l->last_flush_us >= PROTO_LOG_FLUSH_INTERVAL_US) {
        (void)flush_now(l, now_us);
    }
}
