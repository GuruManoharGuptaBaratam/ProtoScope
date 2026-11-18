#include "proto_route.h"
#include "proto_hex.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- helpers */
static proto_event_t serial_event(proto_protocol_t protocol, const char *text, uint64_t id)
{
    proto_uart_frame_t frame;
    proto_event_t ev;
    memset(&frame, 0, sizeof(frame));
    frame.length = strlen(text);
    memcpy(frame.data, text, frame.length);
    frame.completed_us = 1000;
    assert(proto_event_from_serial(&ev, protocol, &frame, 115200, PROTO_DIR_RX));
    ev.event_id = id;
    return ev;
}

static proto_event_t can_event(uint32_t id, const uint8_t *data, uint8_t dlc, uint64_t event_id)
{
    proto_can_frame_t f;
    proto_event_t ev;
    memset(&f, 0, sizeof(f));
    f.id = id; f.dlc = dlc; memcpy(f.data, data, dlc); f.timestamp_us = 2000;
    assert(proto_event_from_can(&ev, &f, PROTO_DIR_RX));
    ev.event_id = event_id;
    return ev;
}

static bool payload_is(const proto_event_t *ev, const char *text)
{
    return ev->payload_length == strlen(text) && memcmp(ev->payload, text, ev->payload_length) == 0;
}

static bool payload_is_hex(const proto_event_t *ev, const char *hex)
{
    uint8_t b[64]; size_t n = 0;
    assert(proto_hex_parse(hex, b, sizeof(b), &n));
    return ev->payload_length == n && memcmp(ev->payload, b, n) == 0;
}

/* ---------------------------------------------------------------- RT-01 vectors */
static proto_proc_result_t rt01(const char *line, proto_event_t *out)
{
    proto_route_t r; proto_route_defaults(&r, 1);
    proto_event_t src = serial_event(PROTO_PROTOCOL_UART, line, 7);
    return proto_route_process(&r, &src, out);
}

static void test_rt01_vectors(void)
{
    proto_event_t out; proto_can_frame_t f;
    assert(rt01("CAN,123,3,0105FF\n", &out) == PROTO_PROC_OK);
    assert(proto_event_validate(&out) == PROTO_EVENT_OK && out.protocol == PROTO_PROTOCOL_CAN);
    assert(out.direction == PROTO_DIR_TX && proto_event_to_can(&out, &f));
    assert(f.id == 0x123 && !f.extended && !f.rtr && f.dlc == 3 &&
           f.data[0] == 0x01 && f.data[1] == 0x05 && f.data[2] == 0xFF);
    assert(out.hop_count == 1 && out.from_protoscope && out.origin_route_id == 1 && out.correlation_id == 7);
    assert(rt01("CAN,7ff,2,aBcD\r\n", &out) == PROTO_PROC_OK);           /* case-insensitive, CRLF */
    assert(proto_event_to_can(&out, &f) && f.id == 0x7FF && f.dlc == 2 && f.data[1] == 0xCD);
    assert(rt01("CAN,123,0,\n", &out) == PROTO_PROC_OK);                  /* DLC 0 */
    assert(proto_event_to_can(&out, &f) && f.dlc == 0);
    assert(rt01("CAN,000,8,0011223344556677\n", &out) == PROTO_PROC_OK);
    /* required negative vectors */
    assert(rt01("CAN,800,1,00\n", &out) == PROTO_PROC_DECODE_ID);         /* id out of range */
    assert(rt01("CAN,123,3,0105\n", &out) == PROTO_PROC_DECODE_LENGTH);   /* payload/DLC mismatch */
    assert(rt01("CAN,XYZ,1,00\n", &out) == PROTO_PROC_DECODE_ID);         /* invalid id */
    assert(rt01("CAN,123,9,000000000000000000\n", &out) == PROTO_PROC_DECODE_DLC);
    /* additional rejection cases from the grammar */
    assert(rt01("CAN,123,1,00,AA\n", &out) == PROTO_PROC_DECODE_FORMAT);  /* extra field */
    assert(rt01("CAN, 123,1,00\n", &out) == PROTO_PROC_DECODE_FORMAT);    /* whitespace */
    assert(rt01("CAN,123,1,0G\n", &out) == PROTO_PROC_DECODE_PAYLOAD);
    assert(rt01("CAN,12,1,00\n", &out) == PROTO_PROC_DECODE_ID);          /* id must be 3 digits */
    assert(rt01("BAD,123,1,00\n", &out) == PROTO_PROC_DECODE_FORMAT);
    assert(rt01("CAN,123,x,00\n", &out) == PROTO_PROC_DECODE_DLC);
    assert(rt01("CAN,123,1\n", &out) == PROTO_PROC_DECODE_FORMAT);
    assert(rt01("\n", &out) == PROTO_PROC_DECODE_FORMAT);
}

/* ---------------------------------------------------------------- default table */
static void test_default_table(void)
{
    int seen[PROTO_PROTOCOL_COUNT][PROTO_PROTOCOL_COUNT];
    memset(seen, 0, sizeof(seen));
    for (uint8_t id = 1; id <= PROTO_ROUTE_COUNT; ++id) {
        proto_route_t r; proto_route_defaults(&r, id);
        assert(r.id == id && !r.enabled && r.source != r.destination);
        seen[r.source][r.destination]++;
    }
    int directed = 0;
    for (int a = 0; a < PROTO_PROTOCOL_COUNT; ++a) {
        for (int b = 0; b < PROTO_PROTOCOL_COUNT; ++b) {
            if (a == b) { assert(seen[a][b] == 0); continue; }
            assert(seen[a][b] == 1);           /* every directed pair exactly once */
            ++directed;
        }
    }
    assert(directed == 20);
    /* RT numbering from route-matrix.md */
    proto_route_t r;
    proto_route_defaults(&r, 1);  assert(r.source == PROTO_PROTOCOL_UART && r.destination == PROTO_PROTOCOL_CAN);
    proto_route_defaults(&r, 2);  assert(r.source == PROTO_PROTOCOL_CAN && r.destination == PROTO_PROTOCOL_UART);
    proto_route_defaults(&r, 19); assert(r.source == PROTO_PROTOCOL_I2C && r.destination == PROTO_PROTOCOL_SPI);
    proto_route_defaults(&r, 20); assert(r.source == PROTO_PROTOCOL_SPI && r.destination == PROTO_PROTOCOL_I2C);
    /* Only routes with a fully defined structural format are enable-able out of the box. */
    for (uint8_t id = 1; id <= PROTO_ROUTE_COUNT; ++id) {
        proto_route_defaults(&r, id);
        const proto_route_check_t c = proto_route_validate(&r);
        if (id == 1 || id == 3 || id == 4) { assert(c == PROTO_ROUTE_OK); continue; }
        assert(c != PROTO_ROUTE_OK);
        if (r.source == PROTO_PROTOCOL_CAN) { assert(c == PROTO_ROUTE_ERR_FILTER_MISSING); }
        else if (r.source == PROTO_PROTOCOL_I2C || r.source == PROTO_PROTOCOL_SPI) { assert(c == PROTO_ROUTE_ERR_POLL_MISSING); }
        else { assert(c == PROTO_ROUTE_ERR_DEST_PARAM); }
    }
    proto_route_defaults(&r, 1); r.destination = PROTO_PROTOCOL_UART;
    assert(proto_route_validate(&r) == PROTO_ROUTE_ERR_SAME_INTERFACE);
    proto_route_defaults(&r, 21); assert(proto_route_validate(&r) == PROTO_ROUTE_ERR_ID);
}

static void test_set_and_validate(void)
{
    proto_route_t r; proto_route_defaults(&r, 9);
    assert(proto_route_set(&r, "filter", "123/7FF") == PROTO_SET_OK && r.has_filter && r.filter_id == 0x123);
    assert(proto_route_set(&r, "filter", "123") == PROTO_SET_BAD_VALUE);
    assert(proto_route_set(&r, "filter", "G/7FF") == PROTO_SET_BAD_VALUE);
    assert(proto_route_set(&r, "i2c_addr", "78") == PROTO_SET_BAD_VALUE);   /* reserved */
    assert(proto_route_set(&r, "i2c_addr", "3C") == PROTO_SET_OK);
    assert(proto_route_validate(&r) == PROTO_ROUTE_ERR_DEST_PARAM);         /* command bytes not chosen */
    assert(proto_route_set(&r, "prefix", "none") == PROTO_SET_OK);
    assert(proto_route_validate(&r) == PROTO_ROUTE_OK);
    assert(proto_route_set(&r, "prefix", "0102030405060708") == PROTO_SET_OK && r.prefix_length == 8);
    assert(proto_route_set(&r, "prefix", "010203040506070809") == PROTO_SET_BAD_VALUE);
    assert(proto_route_set(&r, "nonsense", "1") == PROTO_SET_UNKNOWN_KEY);
    assert(proto_route_set(&r, "term", "tab") == PROTO_SET_BAD_VALUE);
    assert(proto_route_set(&r, "policy", "hold") == PROTO_SET_OK && r.policy == PROTO_POLICY_HOLD);
    assert(proto_route_set(&r, "hold_ms", "10001") == PROTO_SET_BAD_VALUE);
    assert(proto_route_set(&r, "scale", "2:be:u:1:0:0:2:be:u") == PROTO_SET_BAD_VALUE);   /* div 0 */
    assert(proto_route_set(&r, "scale", "3:be:u:1:1:0:2:be:u") == PROTO_SET_BAD_VALUE);   /* width 3 */
    assert(proto_route_set(&r, "scale", "2:be:u:1:10:0:2:le:s") == PROTO_SET_OK && r.mapper == PROTO_MAP_SCALE);
    assert(proto_route_set(&r, "slice", "0:0") == PROTO_SET_BAD_VALUE);

    proto_route_defaults(&r, 8);
    assert(proto_route_set(&r, "poll_i2c", "68:75:2") == PROTO_SET_OK);
    assert(r.poll_i2c.op == PROTO_I2C_OP_WRITE_READ && r.poll_i2c.write_length == 1 && r.poll_i2c.read_length == 2);
    assert(proto_route_validate(&r) == PROTO_ROUTE_OK);
    assert(proto_route_set(&r, "poll_i2c", "68::4") == PROTO_SET_OK && r.poll_i2c.op == PROTO_I2C_OP_READ);
    assert(proto_route_set(&r, "poll_i2c", "02:75:2") == PROTO_SET_BAD_VALUE);
    assert(proto_route_set(&r, "poll_i2c", "68:75:0") == PROTO_SET_BAD_VALUE);
    proto_route_defaults(&r, 10);
    assert(proto_route_set(&r, "poll_i2c", "68:75:2") == PROTO_SET_OK);
    assert(proto_route_validate(&r) == PROTO_ROUTE_ERR_DEST_PARAM);         /* CAN id not set */
    assert(proto_route_set(&r, "can_id", "800") == PROTO_SET_OK);
    assert(proto_route_validate(&r) == PROTO_ROUTE_ERR_DEST_PARAM);         /* 0x800 not standard */
    assert(proto_route_set(&r, "can_ext", "1") == PROTO_SET_OK);
    assert(proto_route_validate(&r) == PROTO_ROUTE_OK);
    proto_route_defaults(&r, 14);
    assert(proto_route_set(&r, "poll_spi", "9F0000") == PROTO_SET_OK && r.poll_spi_length == 3);
    assert(proto_route_validate(&r) == PROTO_ROUTE_OK);
    /* decoder/encoder compatibility */
    proto_route_defaults(&r, 14);
    assert(proto_route_set(&r, "poll_spi", "9F") == PROTO_SET_OK);
    assert(proto_route_set(&r, "dec", "hex") == PROTO_SET_OK);
    assert(proto_route_validate(&r) == PROTO_ROUTE_ERR_DECODER);            /* hex only for serial sources */
    proto_route_defaults(&r, 3);
    assert(proto_route_set(&r, "enc", "can") == PROTO_SET_OK);
    assert(proto_route_validate(&r) == PROTO_ROUTE_ERR_ENCODER);
}

/* ---------------------------------------------------------------- mapping and encoding */
static void test_mappers(void)
{
    proto_route_t r; proto_route_defaults(&r, 3);
    proto_datum_t d; memset(&d, 0, sizeof(d));
    d.length = 4; d.data[0] = 1; d.data[1] = 2; d.data[2] = 3; d.data[3] = 4;
    assert(proto_route_set(&r, "slice", "1:2") == PROTO_SET_OK);
    assert(proto_route_map(&r, &d) == PROTO_PROC_OK && d.length == 2 && d.data[0] == 2 && d.data[1] == 3);
    assert(proto_route_map(&r, &d) == PROTO_PROC_MAP_LENGTH);               /* offset 1 + 2 > length 2 */

    /* u16 big-endian tenths -> s16 little-endian: v' = v/10 - 5 */
    assert(proto_route_set(&r, "scale", "2:be:u:1:10:-5:2:le:s") == PROTO_SET_OK);
    memset(&d, 0, sizeof(d)); d.length = 2; d.data[0] = 0x01; d.data[1] = 0x2C;   /* 300 */
    assert(proto_route_map(&r, &d) == PROTO_PROC_OK && d.length == 2);
    assert(d.data[0] == 25 && d.data[1] == 0);                               /* 300/10 - 5 = 25 */
    memset(&d, 0, sizeof(d)); d.length = 2; d.data[0] = 0x00; d.data[1] = 0x14;   /* 20 -> -3 */
    assert(proto_route_map(&r, &d) == PROTO_PROC_OK && d.data[0] == 0xFD && d.data[1] == 0xFF);
    /* signed input, widening, range checks */
    assert(proto_route_set(&r, "scale", "1:be:s:2:1:0:2:be:s") == PROTO_SET_OK);
    memset(&d, 0, sizeof(d)); d.length = 1; d.data[0] = 0xFF;                 /* -1 -> -2 */
    assert(proto_route_map(&r, &d) == PROTO_PROC_OK && d.length == 2 && d.data[0] == 0xFF && d.data[1] == 0xFE);
    assert(proto_route_set(&r, "scale", "1:be:u:2:1:0:1:be:u") == PROTO_SET_OK);
    memset(&d, 0, sizeof(d)); d.length = 1; d.data[0] = 200;                  /* 400 does not fit u8 */
    assert(proto_route_map(&r, &d) == PROTO_PROC_MAP_RANGE);
    memset(&d, 0, sizeof(d)); d.length = 0;
    assert(proto_route_map(&r, &d) == PROTO_PROC_MAP_LENGTH);
    assert(proto_route_set(&r, "scale", "4:le:u:1:1:0:4:be:u") == PROTO_SET_OK);
    memset(&d, 0, sizeof(d)); d.length = 4; d.data[0] = 0x78; d.data[1] = 0x56; d.data[2] = 0x34; d.data[3] = 0x12;
    assert(proto_route_map(&r, &d) == PROTO_PROC_OK && d.data[0] == 0x12 && d.data[3] == 0x78);
}

static void test_encoders(void)
{
    proto_event_t ev; proto_datum_t d; memset(&d, 0, sizeof(d));
    d.length = 3; d.data[0] = 0x01; d.data[1] = 0xAB; d.data[2] = 0xFF;
    proto_route_t r; proto_route_defaults(&r, 3);                    /* UART -> RS485 */
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK);
    assert(ev.protocol == PROTO_PROTOCOL_RS485 && ev.direction == PROTO_DIR_TX && ev.payload_length == 4 &&
           ev.payload[3] == '\n' && proto_event_validate(&ev) == PROTO_EVENT_OK);
    assert(proto_route_set(&r, "term", "crlf") == PROTO_SET_OK && proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK);
    assert(ev.payload_length == 5 && ev.payload[3] == '\r' && ev.meta.serial.terminator == PROTO_TERM_CRLF);
    assert(proto_route_set(&r, "term", "none") == PROTO_SET_OK && proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK);
    assert(ev.payload_length == 3);
    assert(proto_route_set(&r, "enc", "hex") == PROTO_SET_OK && proto_route_set(&r, "term", "lf") == PROTO_SET_OK);
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK);
    assert(payload_is(&ev, "01ABFF\n"));                                    /* upper-case hex text + LF */
    d.length = 0;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_ENCODE_LENGTH);     /* empty line not allowed */
    d.length = 40;                                                           /* 80 hex chars > 64 */
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_ENCODE_LENGTH);
    d.length = 64;
    assert(proto_route_set(&r, "enc", "raw") == PROTO_SET_OK && proto_route_set(&r, "term", "lf") == PROTO_SET_OK);
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_ENCODE_LENGTH);     /* 64 + LF > 64 */

    /* CAN line representation (RT-02 / RT-05) */
    proto_route_defaults(&r, 2);
    memset(&d, 0, sizeof(d)); d.has_can_id = true; d.can_id = 0x123; d.length = 3; d.data[0] = 1; d.data[1] = 2; d.data[2] = 3;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK && payload_is(&ev, "CAN,123,3,010203\n"));
    d.can_extended = true; d.can_id = 0x1F334455;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK && payload_is(&ev, "CAN,1F334455,3,010203\n"));
    d.can_rtr = true;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_ENCODE_UNREPRESENTABLE);
    d.can_rtr = false; d.has_can_id = false;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_ENCODE_PARAM);

    /* CAN frame destination */
    proto_route_defaults(&r, 10);
    assert(proto_route_set(&r, "can_id", "1ABCDE00") == PROTO_SET_OK && proto_route_set(&r, "can_ext", "1") == PROTO_SET_OK);
    memset(&d, 0, sizeof(d)); d.length = 9;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_ENCODE_LENGTH);     /* CAN max 8 */
    d.length = 2; d.data[0] = 0xAA; d.data[1] = 0xBB;
    proto_can_frame_t f;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK && proto_event_to_can(&ev, &f) &&
           f.id == 0x1ABCDE00 && f.extended && f.dlc == 2);

    /* I2C / SPI destinations: prefix + datum */
    proto_route_defaults(&r, 7);
    assert(proto_route_set(&r, "i2c_addr", "3C") == PROTO_SET_OK && proto_route_set(&r, "prefix", "00") == PROTO_SET_OK);
    memset(&d, 0, sizeof(d)); d.length = 2; d.data[0] = 0xA1; d.data[1] = 0xB2;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK && ev.meta.i2c.address == 0x3C &&
           ev.meta.i2c.op == PROTO_I2C_OP_WRITE && payload_is_hex(&ev, "00A1B2"));
    d.length = 64;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_ENCODE_LENGTH);     /* prefix + 64 > 64 */
    proto_route_defaults(&r, 13);
    assert(proto_route_set(&r, "prefix", "9F") == PROTO_SET_OK);
    d.length = 2;
    assert(proto_route_encode(&r, &d, &ev) == PROTO_PROC_OK && ev.protocol == PROTO_PROTOCOL_SPI &&
           payload_is_hex(&ev, "9FA1B2") && ev.meta.spi.transaction_length == 3);
}

/* ---------------------------------------------------------------- mock interfaces */
#define FIFO 32
typedef struct {
    bool active[PROTO_PROTOCOL_COUNT];
    proto_event_t rx[PROTO_PROTOCOL_COUNT][FIFO];
    size_t rx_head[PROTO_PROTOCOL_COUNT], rx_count[PROTO_PROTOCOL_COUNT];
    proto_io_status_t submit_status[PROTO_PROTOCOL_COUNT];
    int32_t submit_error[PROTO_PROTOCOL_COUNT];
    proto_event_t sent[64];
    size_t sent_count;
    uint8_t i2c_read[64];
    uint8_t spi_rx[64];
    uint64_t now, next_id;
} mock_t;

static bool m_active(void *c, proto_protocol_t p) { return ((mock_t *)c)->active[p]; }
static uint64_t m_now(void *c) { return ((mock_t *)c)->now; }
static uint64_t m_id(void *c) { return ++((mock_t *)c)->next_id; }

static void push_rx(mock_t *m, proto_protocol_t p, const proto_event_t *ev)
{
    assert(m->rx_count[p] < FIFO);
    m->rx[p][(m->rx_head[p] + m->rx_count[p]) % FIFO] = *ev;
    m->rx_count[p]++;
}

static proto_io_status_t m_receive(void *c, proto_protocol_t p, proto_event_t *ev)
{
    mock_t *m = c;
    if (m->rx_count[p] == 0) { return PROTO_IO_EMPTY; }
    *ev = m->rx[p][m->rx_head[p]];
    m->rx_head[p] = (m->rx_head[p] + 1) % FIFO;
    m->rx_count[p]--;
    return PROTO_IO_OK;
}

static proto_io_status_t m_submit(void *c, proto_protocol_t p, const proto_event_t *req,
                                  proto_event_t *res, int32_t *error)
{
    mock_t *m = c;
    if (m->submit_status[p] != PROTO_IO_OK) { *error = m->submit_error[p]; return m->submit_status[p]; }
    assert(m->sent_count < 64);
    m->sent[m->sent_count++] = *req;
    if (p == PROTO_PROTOCOL_I2C && req->meta.i2c.op != PROTO_I2C_OP_WRITE) {
        assert(proto_event_i2c_result(res, req, m->i2c_read, PROTO_EVENT_STATUS_OK, 0, m->now));
    } else if (p == PROTO_PROTOCOL_SPI) {
        assert(proto_event_spi_result(res, req, m->spi_rx, 0, 1000000, PROTO_EVENT_STATUS_OK, 0, m->now));
    }
    return PROTO_IO_OK;
}

static void mock_init(mock_t *m, proto_route_io_t *io)
{
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < PROTO_PROTOCOL_COUNT; ++i) { m->active[i] = true; }
    m->i2c_read[0] = 0x68; m->i2c_read[1] = 0x00;
    m->spi_rx[0] = 0xFF; m->spi_rx[1] = 0xEF; m->spi_rx[2] = 0x40;
    m->now = 1000000; m->next_id = 100;
    io->context = m; io->is_active = m_active; io->receive = m_receive; io->submit = m_submit;
    io->now_us = m_now; io->next_id = m_id;
}

static const proto_event_t *last_sent(const mock_t *m, proto_protocol_t p)
{
    for (size_t i = m->sent_count; i > 0; --i) {
        if (m->sent[i - 1].protocol == p && m->sent[i - 1].direction == PROTO_DIR_TX) { return &m->sent[i - 1]; }
    }
    return NULL;
}

/* Count TX requests sent to a protocol (poll requests to I2C/SPI sources excluded by caller). */
static size_t count_sent(const mock_t *m, proto_protocol_t p)
{
    size_t n = 0;
    for (size_t i = 0; i < m->sent_count; ++i) { n += m->sent[i].protocol == p; }
    return n;
}

static proto_route_engine_t g_engine;

/* ---------------------------------------------------------------- all 20 routes end to end */
typedef struct {
    uint8_t id;
    const char *settings[6];
    const char *stimulus;         /* serial line / "CAN" / "POLL" */
    proto_protocol_t dest;
    const char *expect_text;      /* serial destinations (exact bytes) or NULL */
    const char *expect_hex;       /* payload as hex for binary destinations or NULL */
    uint32_t expect_can_id;
    uint8_t expect_i2c_addr;
} route_case_t;

static const route_case_t k_cases[PROTO_ROUTE_COUNT] = {
    { 1,  { NULL }, "CAN,123,3,0105FF\n", PROTO_PROTOCOL_CAN, NULL, "0105FF", 0x123, 0 },
    { 2,  { "filter=123/7FF", NULL }, "CAN", PROTO_PROTOCOL_UART, "CAN,123,3,010203\n", NULL, 0, 0 },
    { 3,  { NULL }, "HELLO\n", PROTO_PROTOCOL_RS485, "HELLO\n", NULL, 0, 0 },
    { 4,  { NULL }, "WORLD\r\n", PROTO_PROTOCOL_UART, "WORLD\n", NULL, 0, 0 },
    { 5,  { "filter=123/7FF", NULL }, "CAN", PROTO_PROTOCOL_RS485, "CAN,123,3,010203\n", NULL, 0, 0 },
    { 6,  { "can_id=datum", NULL }, "CAN,123,3,0105FF\n", PROTO_PROTOCOL_CAN, NULL, "0105FF", 0x123, 0 },
    { 7,  { "i2c_addr=3C", "prefix=00", NULL }, "A1B2\n", PROTO_PROTOCOL_I2C, NULL, "00A1B2", 0, 0x3C },
    { 8,  { "poll_i2c=68:75:2", NULL }, "POLL", PROTO_PROTOCOL_UART, "6800\n", NULL, 0, 0 },
    { 9,  { "filter=123/7FF", "i2c_addr=3C", "prefix=none", NULL }, "CAN", PROTO_PROTOCOL_I2C, NULL, "010203", 0, 0x3C },
    { 10, { "poll_i2c=68:75:2", "can_id=200", NULL }, "POLL", PROTO_PROTOCOL_CAN, NULL, "6800", 0x200, 0 },
    { 11, { "i2c_addr=3C", "prefix=10", NULL }, "FF\n", PROTO_PROTOCOL_I2C, NULL, "10FF", 0, 0x3C },
    { 12, { "poll_i2c=68:75:2", NULL }, "POLL", PROTO_PROTOCOL_RS485, "6800\n", NULL, 0, 0 },
    { 13, { "prefix=none", NULL }, "9F00\n", PROTO_PROTOCOL_SPI, NULL, "9F00", 0, 0 },
    { 14, { "poll_spi=9F0000", NULL }, "POLL", PROTO_PROTOCOL_UART, "FFEF40\n", NULL, 0, 0 },
    { 15, { "filter=123/7FF", "prefix=none", NULL }, "CAN", PROTO_PROTOCOL_SPI, NULL, "010203", 0, 0 },
    { 16, { "poll_spi=9F0000", "can_id=300", NULL }, "POLL", PROTO_PROTOCOL_CAN, NULL, "FFEF40", 0x300, 0 },
    { 17, { "prefix=AA", NULL }, "0102\n", PROTO_PROTOCOL_SPI, NULL, "AA0102", 0, 0 },
    { 18, { "poll_spi=9F0000", NULL }, "POLL", PROTO_PROTOCOL_RS485, "FFEF40\n", NULL, 0, 0 },
    { 19, { "poll_i2c=68:75:2", "prefix=none", NULL }, "POLL", PROTO_PROTOCOL_SPI, NULL, "6800", 0, 0 },
    { 20, { "poll_spi=9F0000", "i2c_addr=3C", "prefix=none", NULL }, "POLL", PROTO_PROTOCOL_I2C, NULL, "FFEF40", 0, 0x3C },
};

static void test_all_twenty_routes(void)
{
    for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) {
        const route_case_t *c = &k_cases[i];
        mock_t m; proto_route_io_t io;
        mock_init(&m, &io);
        proto_route_engine_init(&g_engine);
        proto_route_t *r = proto_route_engine_route(&g_engine, c->id);
        assert(proto_route_validate(r) != PROTO_ROUTE_OK || c->id == 1 || c->id == 3 || c->id == 4);
        for (size_t s = 0; c->settings[s]; ++s) {
            char key[32], value[64];
            const char *eq = strchr(c->settings[s], '=');
            memcpy(key, c->settings[s], (size_t)(eq - c->settings[s])); key[eq - c->settings[s]] = '\0';
            strcpy(value, eq + 1);
            assert(proto_route_set(r, key, value) == PROTO_SET_OK);
        }
        assert(proto_route_engine_enable(&g_engine, c->id) == PROTO_ROUTE_OK);
        const bool polled = strcmp(c->stimulus, "POLL") == 0;
        if (polled) {
            assert(proto_route_engine_trigger(&g_engine, c->id));
        } else if (strcmp(c->stimulus, "CAN") == 0) {
            const uint8_t data[3] = { 1, 2, 3 };
            proto_event_t ev = can_event(0x123, data, 3, 55);
            push_rx(&m, PROTO_PROTOCOL_CAN, &ev);
        } else {
            proto_event_t ev = serial_event(r->source, c->stimulus, 55);
            push_rx(&m, r->source, &ev);
        }
        proto_route_engine_step(&g_engine, &io);

        const proto_route_stats_t *st = proto_route_engine_stats(&g_engine, c->id);
        assert(st->transmitted == 1 && st->matched == 1);
        assert(st->rejected_decode == 0 && st->rejected_map == 0 && st->rejected_encode == 0 &&
               st->dest_failures == 0 && st->dropped_busy == 0);
        const proto_event_t *out = last_sent(&m, c->dest);
        assert(out != NULL);
        assert(out->protocol == c->dest && out->direction == PROTO_DIR_TX && out->from_protoscope);
        assert(out->origin_route_id == c->id && out->hop_count == 1);
        assert(proto_event_validate(out) == PROTO_EVENT_OK);
        if (c->expect_text) { assert(payload_is(out, c->expect_text)); }
        if (c->expect_hex) { assert(payload_is_hex(out, c->expect_hex)); }
        if (c->dest == PROTO_PROTOCOL_CAN) { assert(out->meta.can.id == c->expect_can_id); }
        if (c->dest == PROTO_PROTOCOL_I2C) { assert(out->meta.i2c.address == c->expect_i2c_addr); }
        /* Every other route stayed untouched. */
        for (uint8_t other = 1; other <= PROTO_ROUTE_COUNT; ++other) {
            if (other == c->id) { continue; }
            assert(proto_route_engine_stats(&g_engine, other)->matched == 0);
        }
        /* Exactly one transmission toward the destination (poll requests go to the source). */
        const proto_route_t *rt = proto_route_engine_route(&g_engine, c->id);
        size_t expected = 1;
        if (polled && rt->source == rt->destination) { expected = 2; }
        assert(count_sent(&m, c->dest) == expected);
    }
}

/* ---------------------------------------------------------------- engine behaviour */
static void test_filtering_and_rejections(void)
{
    mock_t m; proto_route_io_t io; mock_init(&m, &io);
    proto_route_engine_init(&g_engine);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);
    proto_event_t bad = serial_event(PROTO_PROTOCOL_UART, "CAN,800,1,00\n", 1);
    proto_event_t bad2 = serial_event(PROTO_PROTOCOL_UART, "CAN,123,3,0105\n", 2);
    proto_event_t good = serial_event(PROTO_PROTOCOL_UART, "CAN,123,1,AA\n", 3);
    push_rx(&m, PROTO_PROTOCOL_UART, &bad); push_rx(&m, PROTO_PROTOCOL_UART, &bad2); push_rx(&m, PROTO_PROTOCOL_UART, &good);
    proto_route_engine_step(&g_engine, &io);
    const proto_route_stats_t *st = proto_route_engine_stats(&g_engine, 1);
    assert(st->matched == 3 && st->rejected_decode == 2 && st->transmitted == 1);
    assert(st->last_reject == PROTO_PROC_DECODE_LENGTH);
    assert(m.sent_count == 1 && g_engine.iface[PROTO_PROTOCOL_UART].rx_events == 3);

    /* CAN filter: only matching IDs are routed, others counted as filtered. */
    proto_route_engine_init(&g_engine); mock_init(&m, &io);
    proto_route_t *r2 = proto_route_engine_route(&g_engine, 2);
    assert(proto_route_set(r2, "filter", "120/7F0") == PROTO_SET_OK);
    assert(proto_route_engine_enable(&g_engine, 2) == PROTO_ROUTE_OK);
    const uint8_t d[1] = { 9 };
    proto_event_t in = can_event(0x125, d, 1, 5), out_of = can_event(0x200, d, 1, 6);
    push_rx(&m, PROTO_PROTOCOL_CAN, &in); push_rx(&m, PROTO_PROTOCOL_CAN, &out_of);
    proto_route_engine_step(&g_engine, &io);
    st = proto_route_engine_stats(&g_engine, 2);
    assert(st->matched == 1 && st->filtered == 1 && st->transmitted == 1);
}

static void test_disabled_and_unconfigured(void)
{
    mock_t m; proto_route_io_t io; mock_init(&m, &io);
    proto_route_engine_init(&g_engine);
    /* No enabled route: the interface is not drained, so 'msg poll' keeps working. */
    proto_event_t ev = serial_event(PROTO_PROTOCOL_UART, "CAN,123,1,AA\n", 1);
    push_rx(&m, PROTO_PROTOCOL_UART, &ev);
    proto_route_engine_step(&g_engine, &io);
    assert(m.rx_count[PROTO_PROTOCOL_UART] == 1 && m.sent_count == 0);
    /* Unconfigured routes refuse to enable and stay disabled. */
    assert(proto_route_engine_enable(&g_engine, 2) == PROTO_ROUTE_ERR_FILTER_MISSING);
    assert(!proto_route_engine_route(&g_engine, 2)->enabled);
    assert(proto_route_engine_enable(&g_engine, 8) == PROTO_ROUTE_ERR_POLL_MISSING);
    assert(proto_route_engine_enable(&g_engine, 7) == PROTO_ROUTE_ERR_DEST_PARAM);
    assert(proto_route_engine_enable(&g_engine, 0) == PROTO_ROUTE_ERR_ID);
    assert(!proto_route_engine_trigger(&g_engine, 8));                 /* disabled route */
    /* Disabling again stops it. */
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);
    proto_route_engine_disable(&g_engine, 1);
    proto_route_engine_step(&g_engine, &io);
    assert(m.rx_count[PROTO_PROTOCOL_UART] == 1);
    /* Destination not started: rejected and counted, never sent. */
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);
    m.active[PROTO_PROTOCOL_CAN] = false;
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 1)->dest_unavailable == 1 && m.sent_count == 0);
    /* Source not started: nothing is pulled. */
    m.active[PROTO_PROTOCOL_CAN] = true; m.active[PROTO_PROTOCOL_UART] = false;
    push_rx(&m, PROTO_PROTOCOL_UART, &ev);
    proto_route_engine_step(&g_engine, &io);
    assert(m.rx_count[PROTO_PROTOCOL_UART] == 1);
}

static void test_destination_policies(void)
{
    mock_t m; proto_route_io_t io; mock_init(&m, &io);
    proto_event_t a = serial_event(PROTO_PROTOCOL_UART, "CAN,123,1,AA\n", 1);
    proto_event_t b = serial_event(PROTO_PROTOCOL_UART, "CAN,124,1,BB\n", 2);

    /* DROP: reject and count; no retry (RT-01 queue policy). */
    proto_route_engine_init(&g_engine);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);
    m.submit_status[PROTO_PROTOCOL_CAN] = PROTO_IO_BUSY; m.submit_error[PROTO_PROTOCOL_CAN] = 0x107;
    push_rx(&m, PROTO_PROTOCOL_UART, &a); push_rx(&m, PROTO_PROTOCOL_UART, &b);
    proto_route_engine_step(&g_engine, &io);
    const proto_route_stats_t *st = proto_route_engine_stats(&g_engine, 1);
    assert(st->dropped_busy == 2 && st->transmitted == 0 && st->last_error == 0x107 && m.sent_count == 0);
    m.submit_status[PROTO_PROTOCOL_CAN] = PROTO_IO_OK;
    proto_route_engine_step(&g_engine, &io);
    assert(st->transmitted == 0);                                        /* dropped events are gone */

    /* HOLD: keep the request, stop pulling (back-pressure), deliver when free, in order. */
    mock_init(&m, &io); proto_route_engine_init(&g_engine);
    proto_route_t *r = proto_route_engine_route(&g_engine, 1);
    assert(proto_route_set(r, "policy", "hold") == PROTO_SET_OK && proto_route_set(r, "hold_ms", "200") == PROTO_SET_OK);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);
    m.submit_status[PROTO_PROTOCOL_CAN] = PROTO_IO_BUSY;
    push_rx(&m, PROTO_PROTOCOL_UART, &a); push_rx(&m, PROTO_PROTOCOL_UART, &b);
    proto_route_engine_step(&g_engine, &io);
    st = proto_route_engine_stats(&g_engine, 1);
    assert(g_engine.routes[0].pending && m.rx_count[PROTO_PROTOCOL_UART] == 1 && st->transmitted == 0);
    m.now += 50000; proto_route_engine_step(&g_engine, &io);
    assert(g_engine.routes[0].pending && m.rx_count[PROTO_PROTOCOL_UART] == 1);   /* still blocked */
    m.submit_status[PROTO_PROTOCOL_CAN] = PROTO_IO_OK;
    m.now += 10000; proto_route_engine_step(&g_engine, &io);
    assert(st->transmitted == 2 && !g_engine.routes[0].pending && m.rx_count[PROTO_PROTOCOL_UART] == 0);
    assert(m.sent[0].meta.can.id == 0x123 && m.sent[1].meta.can.id == 0x124);     /* order preserved */

    /* HOLD timeout: expires, counted, not silently kept forever. */
    mock_init(&m, &io); proto_route_engine_init(&g_engine);
    r = proto_route_engine_route(&g_engine, 1);
    assert(proto_route_set(r, "policy", "hold") == PROTO_SET_OK && proto_route_set(r, "hold_ms", "100") == PROTO_SET_OK);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);
    m.submit_status[PROTO_PROTOCOL_CAN] = PROTO_IO_BUSY;
    push_rx(&m, PROTO_PROTOCOL_UART, &a);
    proto_route_engine_step(&g_engine, &io);
    m.now += 150000; proto_route_engine_step(&g_engine, &io);
    st = proto_route_engine_stats(&g_engine, 1);
    assert(st->timed_out == 1 && !g_engine.routes[0].pending && st->transmitted == 0);

    /* Hard destination failure is counted with the driver error. */
    mock_init(&m, &io); proto_route_engine_init(&g_engine);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);
    m.submit_status[PROTO_PROTOCOL_CAN] = PROTO_IO_FAILED; m.submit_error[PROTO_PROTOCOL_CAN] = -5;
    push_rx(&m, PROTO_PROTOCOL_UART, &a);
    proto_route_engine_step(&g_engine, &io);
    st = proto_route_engine_stats(&g_engine, 1);
    assert(st->dest_failures == 1 && st->last_error == -5 && !g_engine.routes[0].pending);
}

static void test_fan_out(void)
{
    mock_t m; proto_route_io_t io; mock_init(&m, &io);
    proto_route_engine_init(&g_engine);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);    /* UART -> CAN  */
    assert(proto_route_engine_enable(&g_engine, 3) == PROTO_ROUTE_OK);    /* UART -> RS485 */
    proto_event_t ev = serial_event(PROTO_PROTOCOL_UART, "CAN,123,1,AA\n", 1);
    push_rx(&m, PROTO_PROTOCOL_UART, &ev);
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 1)->transmitted == 1);
    assert(proto_route_engine_stats(&g_engine, 3)->transmitted == 1);
    assert(payload_is(last_sent(&m, PROTO_PROTOCOL_RS485), "CAN,123,1,AA\n"));
    assert(m.sent_count == 2);
    /* A held request on one route blocks pulling for the whole source interface. */
    proto_route_t *r3 = proto_route_engine_route(&g_engine, 3);
    assert(proto_route_set(r3, "policy", "hold") == PROTO_SET_OK);
    m.submit_status[PROTO_PROTOCOL_RS485] = PROTO_IO_BUSY;
    push_rx(&m, PROTO_PROTOCOL_UART, &ev); push_rx(&m, PROTO_PROTOCOL_UART, &ev);
    proto_route_engine_step(&g_engine, &io);
    assert(g_engine.routes[2].pending && m.rx_count[PROTO_PROTOCOL_UART] == 1);
}

static void test_loop_prevention(void)
{
    mock_t m; proto_route_io_t io; mock_init(&m, &io);
    proto_route_engine_init(&g_engine);
    proto_route_t *r2 = proto_route_engine_route(&g_engine, 2);
    assert(proto_route_set(r2, "filter", "000/000") == PROTO_SET_OK);      /* any standard id */
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK);     /* UART -> CAN */
    assert(proto_route_engine_enable(&g_engine, 2) == PROTO_ROUTE_OK);     /* CAN -> UART: a loop with RT-01 */
    proto_event_t line = serial_event(PROTO_PROTOCOL_UART, "CAN,123,3,0105FF\n", 1);
    push_rx(&m, PROTO_PROTOCOL_UART, &line);
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 1)->transmitted == 1);
    /* The CAN controller reflects our own frame back (self reception / bus echo). */
    const uint8_t data[3] = { 0x01, 0x05, 0xFF };
    proto_event_t echo = can_event(0x123, data, 3, 9);
    m.now += 5000;
    push_rx(&m, PROTO_PROTOCOL_CAN, &echo);
    proto_route_engine_step(&g_engine, &io);
    assert(g_engine.iface[PROTO_PROTOCOL_CAN].loop_suppressed == 1);
    assert(proto_route_engine_stats(&g_engine, 2)->matched == 0 && count_sent(&m, PROTO_PROTOCOL_UART) == 0);
    /* The same frame from a real node after the window is routed normally. */
    m.now += 500000;
    push_rx(&m, PROTO_PROTOCOL_CAN, &echo);
    proto_route_engine_step(&g_engine, &io);
    assert(g_engine.iface[PROTO_PROTOCOL_CAN].loop_suppressed == 1);
    assert(proto_route_engine_stats(&g_engine, 2)->transmitted == 1);
    /* A different frame inside the window is not an echo. */
    mock_init(&m, &io); proto_route_engine_init(&g_engine);
    assert(proto_route_set(proto_route_engine_route(&g_engine, 2), "filter", "000/000") == PROTO_SET_OK);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK && proto_route_engine_enable(&g_engine, 2) == PROTO_ROUTE_OK);
    push_rx(&m, PROTO_PROTOCOL_UART, &line);
    proto_route_engine_step(&g_engine, &io);
    const uint8_t other[1] = { 0x77 };
    proto_event_t different = can_event(0x123, other, 1, 10);
    push_rx(&m, PROTO_PROTOCOL_CAN, &different);
    proto_route_engine_step(&g_engine, &io);
    assert(g_engine.iface[PROTO_PROTOCOL_CAN].loop_suppressed == 0 && proto_route_engine_stats(&g_engine, 2)->transmitted == 1);
    /* Window 0 disables suppression. */
    mock_init(&m, &io); proto_route_engine_init(&g_engine);
    g_engine.loop_window_ms = 0;
    assert(proto_route_set(proto_route_engine_route(&g_engine, 2), "filter", "000/000") == PROTO_SET_OK);
    assert(proto_route_engine_enable(&g_engine, 1) == PROTO_ROUTE_OK && proto_route_engine_enable(&g_engine, 2) == PROTO_ROUTE_OK);
    push_rx(&m, PROTO_PROTOCOL_UART, &line);
    proto_route_engine_step(&g_engine, &io);
    push_rx(&m, PROTO_PROTOCOL_CAN, &echo);
    proto_route_engine_step(&g_engine, &io);
    assert(g_engine.iface[PROTO_PROTOCOL_CAN].loop_suppressed == 0);
    /* Provenance guard: events marked as ProtoScope's own, or past the hop limit, are refused. */
    mock_init(&m, &io); proto_route_engine_init(&g_engine);
    assert(proto_route_engine_enable(&g_engine, 3) == PROTO_ROUTE_OK);
    proto_event_t own = serial_event(PROTO_PROTOCOL_UART, "X\n", 20); own.from_protoscope = true;
    proto_event_t hops = serial_event(PROTO_PROTOCOL_UART, "Y\n", 21); hops.hop_count = PROTO_EVENT_MAX_HOPS;
    push_rx(&m, PROTO_PROTOCOL_UART, &own); push_rx(&m, PROTO_PROTOCOL_UART, &hops);
    proto_route_engine_step(&g_engine, &io);
    assert(g_engine.iface[PROTO_PROTOCOL_UART].loop_rejected == 2 && m.sent_count == 0);
}

static void test_polling(void)
{
    mock_t m; proto_route_io_t io; mock_init(&m, &io);
    proto_route_engine_init(&g_engine);
    proto_route_t *r = proto_route_engine_route(&g_engine, 8);
    assert(proto_route_set(r, "poll_i2c", "68:75:2") == PROTO_SET_OK && proto_route_set(r, "interval", "100") == PROTO_SET_OK);
    assert(proto_route_engine_enable(&g_engine, 8) == PROTO_ROUTE_OK);
    proto_route_engine_step(&g_engine, &io);                                  /* due immediately */
    assert(proto_route_engine_stats(&g_engine, 8)->polls == 1);
    m.now += 50000; proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 8)->polls == 1);               /* not yet */
    m.now += 50000; proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 8)->polls == 2 && proto_route_engine_stats(&g_engine, 8)->transmitted == 2);
    /* Manual trigger when interval is 0; polls do nothing otherwise. */
    mock_init(&m, &io); proto_route_engine_init(&g_engine);
    r = proto_route_engine_route(&g_engine, 14);
    assert(proto_route_set(r, "poll_spi", "9F0000") == PROTO_SET_OK);
    assert(proto_route_engine_enable(&g_engine, 14) == PROTO_ROUTE_OK);
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 14)->polls == 0);
    assert(proto_route_engine_trigger(&g_engine, 14));
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 14)->polls == 1 && proto_route_engine_stats(&g_engine, 14)->transmitted == 1);
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 14)->polls == 1);              /* one-shot */
    /* A failing source read is counted and not forwarded. */
    m.submit_status[PROTO_PROTOCOL_SPI] = PROTO_IO_FAILED; m.submit_error[PROTO_PROTOCOL_SPI] = -9;
    assert(proto_route_engine_trigger(&g_engine, 14));
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 14)->poll_failures == 1 && proto_route_engine_stats(&g_engine, 14)->last_error == -9);
    /* Source interface not started. */
    m.submit_status[PROTO_PROTOCOL_SPI] = PROTO_IO_OK; m.active[PROTO_PROTOCOL_SPI] = false;
    assert(proto_route_engine_trigger(&g_engine, 14));
    proto_route_engine_step(&g_engine, &io);
    assert(proto_route_engine_stats(&g_engine, 14)->dest_unavailable == 1);
    /* Stats reset. */
    proto_route_engine_reset_stats(&g_engine);
    assert(proto_route_engine_stats(&g_engine, 14)->polls == 0 && g_engine.steps == 0);
}

static void test_mapping_in_engine(void)
{
    mock_t m; proto_route_io_t io; mock_init(&m, &io);
    proto_route_engine_init(&g_engine);
    proto_route_t *r = proto_route_engine_route(&g_engine, 10);              /* I2C -> CAN with scaling */
    assert(proto_route_set(r, "poll_i2c", "68:75:2") == PROTO_SET_OK && proto_route_set(r, "can_id", "200") == PROTO_SET_OK);
    assert(proto_route_set(r, "scale", "2:be:u:1:1:100:2:be:u") == PROTO_SET_OK);      /* +100 */
    m.i2c_read[0] = 0x00; m.i2c_read[1] = 0x14;                                        /* 20 */
    assert(proto_route_engine_enable(&g_engine, 10) == PROTO_ROUTE_OK && proto_route_engine_trigger(&g_engine, 10));
    proto_route_engine_step(&g_engine, &io);
    assert(payload_is_hex(last_sent(&m, PROTO_PROTOCOL_CAN), "0078"));                 /* 120 */
    /* Out-of-range scaling is rejected, never transmitted. */
    assert(proto_route_set(r, "scale", "2:be:u:1000:1:0:2:be:u") == PROTO_SET_OK);
    m.i2c_read[0] = 0xFF; m.i2c_read[1] = 0xFF;
    const size_t before = m.sent_count;
    assert(proto_route_engine_trigger(&g_engine, 10));
    proto_route_engine_step(&g_engine, &io);
    const proto_route_stats_t *st = proto_route_engine_stats(&g_engine, 10);
    assert(st->rejected_map == 1 && st->last_reject == PROTO_PROC_MAP_RANGE);
    assert(count_sent(&m, PROTO_PROTOCOL_CAN) == 1 && m.sent_count == before + 1);    /* only the poll */
}

int main(void)
{
    test_rt01_vectors();
    test_default_table();
    test_set_and_validate();
    test_mappers();
    test_encoders();
    test_all_twenty_routes();
    test_filtering_and_rejections();
    test_disabled_and_unconfigured();
    test_destination_policies();
    test_fan_out();
    test_loop_prevention();
    test_polling();
    test_mapping_in_engine();
    puts("Routing engine: RT-01 vectors, 20-route table, configuration, mapping, encoders, all 20 directed routes, policies, loop guard and polling passed.");
    return 0;
}
