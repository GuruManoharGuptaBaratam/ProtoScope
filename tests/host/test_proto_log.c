#include "proto_logger.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- mock card */
#define MAX_FILES 8
#define FILE_CAP (256 * 1024)

typedef struct {
    bool present;
    size_t free_bytes;           /* remaining space on the "card" */
    int fail_write_after;        /* >=0: fail the Nth next write with IO error; -1 never */
    int fail_flush;
    struct { char name[16]; char *data; size_t length; bool used; } files[MAX_FILES];
    int current;
    int opens, closes, flushes;
    uint16_t preexisting_max;
} card_t;

static int c_list(void *ctx, uint16_t *max)
{
    card_t *c = ctx;
    if (!c->present) { return PROTO_LOG_ERR_NO_CARD; }
    uint16_t best = c->preexisting_max;
    for (int i = 0; i < MAX_FILES; ++i) {
        uint16_t idx;
        if (c->files[i].used && proto_log_parse_index(c->files[i].name, &idx) && idx > best) { best = idx; }
    }
    *max = best;
    return PROTO_LOG_OK;
}

static int c_open(void *ctx, const char *name)
{
    card_t *c = ctx;
    if (!c->present) { return PROTO_LOG_ERR_NO_CARD; }
    for (int i = 0; i < MAX_FILES; ++i) {
        if (!c->files[i].used) {
            c->files[i].used = true;
            strcpy(c->files[i].name, name);
            c->files[i].length = 0;
            c->current = i;
            c->opens++;
            return PROTO_LOG_OK;
        }
    }
    return PROTO_LOG_ERR_FULL;
}

static int c_write(void *ctx, const char *data, size_t n)
{
    card_t *c = ctx;
    if (!c->present) { return PROTO_LOG_ERR_IO; }
    if (c->fail_write_after == 0) { c->fail_write_after = -1; return PROTO_LOG_ERR_IO; }
    if (c->fail_write_after > 0) { c->fail_write_after--; }
    if (n > c->free_bytes) { return PROTO_LOG_ERR_FULL; }
    assert(c->files[c->current].length + n <= FILE_CAP);
    memcpy(c->files[c->current].data + c->files[c->current].length, data, n);
    c->files[c->current].length += n;
    c->free_bytes -= n;
    return PROTO_LOG_OK;
}

static int c_flush(void *ctx)
{
    card_t *c = ctx;
    c->flushes++;
    return c->fail_flush ? PROTO_LOG_ERR_IO : PROTO_LOG_OK;
}

static void c_close(void *ctx) { ((card_t *)ctx)->closes++; }

static card_t g_card;
static char g_store[MAX_FILES][FILE_CAP];
static proto_event_t g_slots[16];
static proto_logger_t g_log;

static void setup(size_t free_bytes)
{
    memset(&g_card, 0, sizeof(g_card));
    for (int i = 0; i < MAX_FILES; ++i) { g_card.files[i].data = g_store[i]; }
    g_card.present = true; g_card.free_bytes = free_bytes; g_card.fail_write_after = -1;
    const proto_log_sink_t sink = { &g_card, c_list, c_open, c_write, c_flush, c_close };
    proto_logger_init(&g_log, &sink, "0.8.0", g_slots, 16);
}

static proto_event_t can_ev(uint32_t id, uint64_t eid)
{
    proto_can_frame_t f; proto_event_t e;
    memset(&f, 0, sizeof(f));
    f.id = id; f.dlc = 3; f.data[0] = 1; f.data[1] = 5; f.data[2] = 0xFF; f.timestamp_us = 1000 + eid;
    assert(proto_event_from_can(&e, &f, PROTO_DIR_RX));
    e.event_id = eid;
    return e;
}

static size_t lines_in(const char *data, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) { n += data[i] == '\n'; }
    return n;
}

/* ---------------------------------------------------------------- tests */
static void test_format(void)
{
    char out[PROTO_LOG_LINE_MAX];
    proto_event_t e = can_ev(0x123, 77);
    e.correlation_id = 5; e.hop_count = 1; e.origin_route_id = 2;
    const size_t n = proto_log_format_record(&e, out, sizeof(out));
    assert(n > 0 && out[n - 1] == '\n' && strchr(out, '\n') == &out[n - 1]);
    assert(strcmp(out, "1077,77,5,CAN,RX,OK,0,1,2,3,0105FF,id=123;ext=0;rtr=0;dlc=3\n") == 0);
    assert(proto_log_format_record(&e, out, 10) == 0);                       /* too small */
    proto_event_t bad = e; bad.protocol = PROTO_PROTOCOL_COUNT;
    assert(proto_log_format_record(&bad, out, sizeof(out)) == 0);

    proto_uart_frame_t f; memset(&f, 0, sizeof(f));
    memcpy(f.data, "PING\r\n", 6); f.length = 6; f.completed_us = 9;
    assert(proto_event_from_serial(&e, PROTO_PROTOCOL_RS485, &f, 9600, PROTO_DIR_RX));
    e.event_id = 3;
    assert(proto_log_format_record(&e, out, sizeof(out)) > 0);
    assert(strcmp(out, "9,3,0,RS485,RX,OK,0,0,0,6,50494E470D0A,baud=9600;term=CRLF\n") == 0);
    uint8_t tx[2] = { 0x9F, 0x00 };
    assert(proto_event_from_spi_request(&e, tx, 2, 0, 1000000));
    assert(proto_log_format_record(&e, out, sizeof(out)) > 0 && strstr(out, ",SPI,TX,OK,") && strstr(out, "cs=0;mode=0;clock_hz=1000000;len=2"));
    proto_i2c_txn_t t; memset(&t, 0, sizeof(t)); t.address = 0x68; t.op = PROTO_I2C_OP_WRITE_READ;
    t.write_length = 1; t.read_length = 2; t.write_data[0] = 0x75;
    assert(proto_event_from_i2c_request(&e, &t));
    assert(proto_log_format_record(&e, out, sizeof(out)) > 0 && strstr(out, "addr=0x68;op=WRITE_READ;wr=1;rd=2;ack=UNKNOWN"));
    char header[256];
    assert(proto_log_format_header("0.8.0", header, sizeof(header)) > 50 && strstr(header, "schema=1 firmware=0.8.0") &&
           strstr(header, "microseconds since boot") && strstr(header, "timestamp_us,event_id"));
    assert(proto_log_format_header("0.8.0", header, 20) == 0);

    uint16_t idx = 0;
    assert(proto_log_parse_index("LOG0007.CSV", &idx) && idx == 7);
    assert(proto_log_parse_index("log0120.csv", &idx) && idx == 120);
    assert(!proto_log_parse_index("LOG0000.CSV", &idx) && !proto_log_parse_index("LOG7.CSV", &idx));
    assert(!proto_log_parse_index("LOG00A7.CSV", &idx) && !proto_log_parse_index("LOG0007.TXT", &idx));
    assert(!proto_log_parse_index("NOTLOG0007.CSV", &idx) && !proto_log_parse_index(NULL, &idx));
    char name[PROTO_LOG_NAME_MAX]; proto_log_make_name(42, name, sizeof(name));
    assert(strcmp(name, "LOG0042.CSV") == 0);
}

static void test_queue(void)
{
    proto_event_t slots[3], e = can_ev(1, 1), out;
    proto_log_queue_t q; proto_log_queue_init(&q, slots, 3);
    assert(proto_log_queue_push(&q, &e) && proto_log_queue_push(&q, &e) && proto_log_queue_push(&q, &e));
    assert(!proto_log_queue_push(&q, &e) && q.dropped == 1 && q.high_water == 3);
    e.event_id = 99;
    assert(!proto_log_queue_push(&q, &e) && q.dropped == 2);                  /* newest is dropped */
    assert(proto_log_queue_pop(&q, &out) && out.event_id == 1);
    e.event_id = 4; assert(proto_log_queue_push(&q, &e));                     /* wraps */
    assert(proto_log_queue_pop(&q, &out) && proto_log_queue_pop(&q, &out) && proto_log_queue_pop(&q, &out) && out.event_id == 4);
    assert(!proto_log_queue_pop(&q, &out));
}

static void test_normal_run(void)
{
    setup(1u << 20);
    assert(proto_logger_start(&g_log, 0) && g_log.state == PROTO_LOG_STATE_RUNNING);
    assert(strcmp(g_log.file_name, "LOG0001.CSV") == 0 && g_log.files_created == 1);
    for (uint64_t i = 1; i <= 5; ++i) { proto_event_t e = can_ev(0x100 + i, i); assert(proto_logger_submit(&g_log, &e)); }
    proto_logger_service(&g_log, 100000);
    assert(g_log.records_written == 5 && g_log.queue.count == 0);
    assert(g_card.flushes == 0);                                              /* not yet: <32 records, <1 s */
    proto_logger_service(&g_log, 1200000);
    assert(g_card.flushes == 1 && g_log.unflushed == 0);
    const size_t len = g_card.files[0].length;
    assert(lines_in(g_card.files[0].data, len) == 3 + 5);                     /* 2 comment + column row + 5 records */
    assert(strstr(g_card.files[0].data, "1001,1,0,CAN,RX,OK,0,0,0,3,0105FF,id=101;ext=0;rtr=0;dlc=3\n"));
    /* stop writes the rest, flushes and closes; filter and 'wanted' gate submissions */
    proto_event_t e = can_ev(0x200, 9); assert(proto_logger_submit(&g_log, &e));
    proto_logger_stop(&g_log, 2000000);
    assert(g_log.state == PROTO_LOG_STATE_OFF && g_card.closes == 1 && g_log.records_written == 6);
    assert(!proto_logger_submit(&g_log, &e));
    /* a second session numbers the next file */
    assert(proto_logger_start(&g_log, 3000000) && strcmp(g_log.file_name, "LOG0002.CSV") == 0);
    /* protocol mask */
    g_log.protocol_mask = PROTO_LOG_MASK(PROTO_PROTOCOL_UART);
    assert(!proto_logger_submit(&g_log, &e) && g_log.dropped_not_running == 0);
    g_log.protocol_mask = PROTO_LOG_MASK_ALL;
    /* existing files on the card are never overwritten */
    setup(1u << 20); g_card.preexisting_max = 41;
    assert(proto_logger_start(&g_log, 0) && strcmp(g_log.file_name, "LOG0042.CSV") == 0);
}

static void test_flush_by_count(void)
{
    setup(1u << 20);
    assert(proto_logger_start(&g_log, 0));
    for (uint64_t i = 1; i <= 40; ++i) {
        proto_event_t e = can_ev(0x100, i);
        if (!proto_logger_submit(&g_log, &e)) { proto_logger_service(&g_log, i); assert(proto_logger_submit(&g_log, &e)); }
    }
    proto_logger_service(&g_log, 50);
    assert(g_card.flushes >= 1 && g_log.records_written == 40);
}

static void test_queue_overflow_counted(void)
{
    setup(1u << 20);
    assert(proto_logger_start(&g_log, 0));
    for (uint64_t i = 1; i <= 20; ++i) { proto_event_t e = can_ev(0x100, i); (void)proto_logger_submit(&g_log, &e); }
    assert(g_log.queue.dropped == 4 && proto_logger_dropped_total(&g_log) == 4);   /* 16 slots, 20 events */
    proto_logger_service(&g_log, 10);
    assert(g_log.records_written == 16);                                            /* none silently lost */
}

static void test_rotation(void)
{
    setup(1u << 20);
    g_log.max_file_bytes = 700;
    assert(proto_logger_start(&g_log, 0));
    for (int round = 0; round < 6; ++round) {
        for (uint64_t i = 1; i <= 8; ++i) { proto_event_t e = can_ev(0x100, (uint64_t)round * 8 + i); assert(proto_logger_submit(&g_log, &e)); }
        proto_logger_service(&g_log, (uint64_t)(round + 1) * 10000);
    }
    assert(g_log.records_written == 48 && g_log.files_created >= 3);
    for (int i = 0; i < MAX_FILES; ++i) {
        if (!g_card.files[i].used) { continue; }
        assert(g_card.files[i].length <= 700);
        assert(strncmp(g_card.files[i].data, "# ProtoScope log schema=1", 25) == 0);   /* every file has its header */
    }
    assert(strcmp(g_log.file_name, "LOG0001.CSV") != 0);
}

static void test_no_card_then_card(void)
{
    setup(1u << 20);
    g_card.present = false;
    assert(!proto_logger_start(&g_log, 0) && g_log.state == PROTO_LOG_STATE_NO_CARD && g_log.wanted);
    proto_event_t e = can_ev(0x100, 1);
    assert(!proto_logger_submit(&g_log, &e) && g_log.dropped_not_running == 1);     /* counted, not silent */
    proto_logger_service(&g_log, 2000000);
    assert(g_log.state == PROTO_LOG_STATE_NO_CARD);                                 /* retry only every 5 s */
    g_card.present = true;
    proto_logger_service(&g_log, 6000000);
    assert(g_log.state == PROTO_LOG_STATE_RUNNING && strcmp(g_log.file_name, "LOG0001.CSV") == 0);
    assert(proto_logger_submit(&g_log, &e));
    proto_logger_stop(&g_log, 7000000);
}

static void test_write_failure_and_recovery(void)
{
    setup(1u << 20);
    assert(proto_logger_start(&g_log, 0));
    for (uint64_t i = 1; i <= 4; ++i) { proto_event_t e = can_ev(0x100, i); assert(proto_logger_submit(&g_log, &e)); }
    g_card.fail_write_after = 2;                                                    /* third write fails */
    proto_logger_service(&g_log, 1000);
    assert(g_log.state == PROTO_LOG_STATE_ERROR && g_log.write_errors == 1);
    assert(g_log.records_written == 2 && g_log.dropped_write_failed == 2);          /* 1 failed + 1 discarded */
    assert(proto_logger_dropped_total(&g_log) == 2);
    proto_event_t e = can_ev(0x100, 9);
    assert(!proto_logger_submit(&g_log, &e) && g_log.dropped_not_running == 1);
    proto_logger_service(&g_log, 3000000);
    assert(g_log.state == PROTO_LOG_STATE_ERROR);                                   /* waits 5 s */
    proto_logger_service(&g_log, 6000000);
    assert(g_log.state == PROTO_LOG_STATE_RUNNING && strcmp(g_log.file_name, "LOG0002.CSV") == 0);
    assert(proto_logger_submit(&g_log, &e));
    /* a flush failure is an error too */
    g_card.fail_flush = 1;
    proto_logger_service(&g_log, 8000000);
    assert(g_log.state == PROTO_LOG_STATE_ERROR);
    /* card pulled while running */
    g_card.fail_flush = 0; g_card.present = true;
    proto_logger_service(&g_log, 20000000);
    assert(g_log.state == PROTO_LOG_STATE_RUNNING);
    g_card.present = false;
    proto_event_t e2 = can_ev(0x100, 10); assert(proto_logger_submit(&g_log, &e2));
    proto_logger_service(&g_log, 20100000);
    assert(g_log.state == PROTO_LOG_STATE_ERROR);
}

static void test_full_card(void)
{
    setup(300);                                                                     /* header fits, little else */
    assert(proto_logger_start(&g_log, 0));
    for (uint64_t i = 1; i <= 10; ++i) { proto_event_t e = can_ev(0x100, i); assert(proto_logger_submit(&g_log, &e)); }
    proto_logger_service(&g_log, 1000);
    assert(g_log.state == PROTO_LOG_STATE_FULL && g_log.write_errors == 1);
    assert(g_log.records_written < 10);
    proto_event_t e = can_ev(0x100, 11);
    assert(!proto_logger_submit(&g_log, &e));
    g_card.free_bytes = 1u << 20;                                                   /* space freed */
    proto_logger_service(&g_log, 10000000);
    assert(g_log.state == PROTO_LOG_STATE_FULL);                                    /* FULL retries every 30 s */
    proto_logger_service(&g_log, 31000000);
    assert(g_log.state == PROTO_LOG_STATE_RUNNING);
    /* card that cannot even hold the header, and file numbers exhausted */
    setup(10);
    assert(!proto_logger_start(&g_log, 0) && g_log.state == PROTO_LOG_STATE_FULL);
    setup(1u << 20); g_card.preexisting_max = PROTO_LOG_INDEX_MAX;
    assert(!proto_logger_start(&g_log, 0) && g_log.state == PROTO_LOG_STATE_FULL);
}

int main(void)
{
    test_format();
    test_queue();
    test_normal_run();
    test_flush_by_count();
    test_queue_overflow_counted();
    test_rotation();
    test_no_card_then_card();
    test_write_failure_and_recovery();
    test_full_card();
    puts("Logger: CSV schema, queue, rotation, flushing, no-card/full/write-failure handling and drop counters passed.");
    return 0;
}
