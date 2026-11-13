#include "proto_event.h"
#include "proto_event_caps.h"
#include "proto_hex.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_hex(void)
{
    uint8_t b[4]; size_t n = 0;
    assert(proto_hex_parse("0105Ff", b, sizeof(b), &n) && n == 3 && b[2] == 0xFF);
    assert(!proto_hex_parse("", b, sizeof(b), &n) && !proto_hex_parse("012", b, sizeof(b), &n));
    assert(!proto_hex_parse("0G", b, sizeof(b), &n) && !proto_hex_parse("0102030405", b, 4, &n));
    char out[16];
    assert(proto_hex_format(b, 3, out, sizeof(out)) == 6 && strcmp(out, "0105FF") == 0);
    assert(proto_hex_format(b, 3, out, 6) == 0);
}

static void test_caps(void)
{
    proto_protocol_t p;
    assert(proto_protocol_parse("can", &p) && p == PROTO_PROTOCOL_CAN);
    assert(proto_protocol_parse("RS-485", &p) && p == PROTO_PROTOCOL_RS485);
    assert(proto_protocol_parse("rs485", &p) && p == PROTO_PROTOCOL_RS485);
    assert(proto_protocol_parse("I2c", &p) && p == PROTO_PROTOCOL_I2C);
    assert(!proto_protocol_parse("modbus", &p) && !proto_protocol_parse(NULL, &p));
    assert(proto_protocol_max_payload(PROTO_PROTOCOL_CAN) == 8);
    assert(proto_protocol_max_payload(PROTO_PROTOCOL_SPI) == 64);
    /* Every interface can transmit; only UART/RS485/CAN have unsolicited RX. */
    for (int i = 0; i < PROTO_PROTOCOL_COUNT; ++i) {
        assert(proto_protocol_caps((proto_protocol_t)i) & PROTO_CAP_TX);
    }
    assert(proto_protocol_caps(PROTO_PROTOCOL_UART) & PROTO_CAP_RX_EVENT);
    assert(proto_protocol_caps(PROTO_PROTOCOL_CAN) & PROTO_CAP_RX_EVENT);
    assert(!(proto_protocol_caps(PROTO_PROTOCOL_I2C) & PROTO_CAP_RX_EVENT));
    assert(!(proto_protocol_caps(PROTO_PROTOCOL_SPI) & PROTO_CAP_RX_EVENT));
    assert(proto_protocol_caps(PROTO_PROTOCOL_I2C) & PROTO_CAP_CONTROLLER_ROLE);
    assert(proto_protocol_caps(PROTO_PROTOCOL_SPI) & PROTO_CAP_FULL_DUPLEX_TRANSFER);
    assert(proto_protocol_caps(PROTO_PROTOCOL_RS485) & PROTO_CAP_HALF_DUPLEX);
    char text[96];
    assert(proto_caps_format(proto_protocol_caps(PROTO_PROTOCOL_SPI), text, sizeof(text)) > 0);
    assert(strstr(text, "CONTROLLER_ONLY") && strstr(text, "FULL_DUPLEX"));
}

static void test_can_roundtrip(void)
{
    const char *cases[] = { "123#0105FF", "1F334455#11", "7FF#", "100#R", "1FFFFFFF#0011223344556677" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        proto_can_frame_t in, out; proto_event_t ev;
        assert(proto_can_parse_frame(cases[i], &in) == PROTO_CAN_PARSE_OK);
        in.timestamp_us = 1234 + i;
        assert(proto_event_from_can(&ev, &in, PROTO_DIR_RX));
        assert(proto_event_validate(&ev) == PROTO_EVENT_OK);
        assert(ev.timestamp_us == 1234 + i && ev.protocol == PROTO_PROTOCOL_CAN);
        assert(proto_event_to_can(&ev, &out));
        assert(out.id == in.id && out.extended == in.extended && out.rtr == in.rtr &&
               out.dlc == in.dlc && memcmp(out.data, in.data, in.dlc) == 0);
        char a[PROTO_CAN_TEXT_MAX];
        assert(proto_can_format_frame(&out, a, sizeof(a)) && strcmp(a, cases[i]) == 0);
    }
    proto_can_frame_t bad = { .id = 0x800, .extended = false, .dlc = 1 };
    proto_event_t ev;
    assert(!proto_event_from_can(&ev, &bad, PROTO_DIR_TX));
}

static proto_uart_frame_t frame_of(const char *s)
{
    proto_uart_frame_t f; memset(&f, 0, sizeof(f));
    f.length = strlen(s); memcpy(f.data, s, f.length); f.completed_us = 777;
    return f;
}

static void test_serial_preserves_bytes(void)
{
    const char *lines[] = { "HELLO\r\n", "ONE\n", "RAW" };
    const proto_serial_terminator_t terms[] = { PROTO_TERM_CRLF, PROTO_TERM_LF, PROTO_TERM_NONE };
    for (int proto = PROTO_PROTOCOL_UART; proto <= PROTO_PROTOCOL_RS485; ++proto) {
        for (int i = 0; i < 3; ++i) {
            proto_uart_frame_t f = frame_of(lines[i]); proto_event_t ev;
            assert(proto_event_from_serial(&ev, (proto_protocol_t)proto, &f, 9600, PROTO_DIR_RX));
            assert(ev.meta.serial.terminator == terms[i] && ev.meta.serial.baud_rate == 9600);
            assert(ev.timestamp_us == 777 && proto_event_validate(&ev) == PROTO_EVENT_OK);
            uint8_t out[64]; size_t n = 0;
            assert(proto_event_to_serial(&ev, out, sizeof(out), &n));
            assert(n == f.length && memcmp(out, f.data, n) == 0);
        }
    }
    proto_event_t ev; proto_uart_frame_t f = frame_of("A\n");
    assert(!proto_event_from_serial(&ev, PROTO_PROTOCOL_CAN, &f, 9600, PROTO_DIR_RX));
    f.length = 0;
    assert(!proto_event_from_serial(&ev, PROTO_PROTOCOL_UART, &f, 9600, PROTO_DIR_RX));
    /* A TX request may leave baud 0 (= configured); an RX event may not. */
    f = frame_of("X\n");
    assert(proto_event_from_serial(&ev, PROTO_PROTOCOL_UART, &f, 0, PROTO_DIR_TX));
    assert(proto_event_validate(&ev) == PROTO_EVENT_OK);
    assert(proto_event_from_serial(&ev, PROTO_PROTOCOL_UART, &f, 0, PROTO_DIR_RX));
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_METADATA);
    /* Terminator metadata must match the payload. */
    assert(proto_event_from_serial(&ev, PROTO_PROTOCOL_UART, &f, 9600, PROTO_DIR_RX));
    ev.meta.serial.terminator = PROTO_TERM_CRLF;
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_METADATA);
}

static void test_i2c(void)
{
    proto_i2c_txn_t txn; memset(&txn, 0, sizeof(txn));
    txn.address = 0x68; txn.op = PROTO_I2C_OP_WRITE_READ; txn.write_length = 1;
    txn.read_length = 2; txn.write_data[0] = 0x75;
    proto_event_t req, res, again;
    assert(proto_event_from_i2c_request(&req, &txn));
    req.event_id = 41; req.device_id = 3;
    assert(req.direction == PROTO_DIR_TX && req.payload_length == 1 && req.payload[0] == 0x75);
    assert(proto_event_validate(&req) == PROTO_EVENT_OK);
    proto_i2c_txn_t back;
    assert(proto_event_to_i2c(&req, &back));
    assert(back.address == 0x68 && back.op == PROTO_I2C_OP_WRITE_READ && back.write_length == 1 &&
           back.read_length == 2 && back.write_data[0] == 0x75);

    const uint8_t data[2] = { 0x68, 0x00 };
    assert(proto_event_i2c_result(&res, &req, data, PROTO_EVENT_STATUS_OK, 0, 5000));
    assert(res.direction == PROTO_DIR_RX && res.correlation_id == 41 && res.device_id == 3);
    assert(res.meta.i2c.ack == PROTO_I2C_ACK_ACK && res.payload_length == 2 && res.payload[0] == 0x68);
    assert(res.meta.i2c.write_length == 1 && res.meta.i2c.read_length == 2); /* context kept */
    assert(proto_event_validate(&res) == PROTO_EVENT_OK);

    assert(proto_event_i2c_result(&again, &req, NULL, PROTO_EVENT_STATUS_NACK, -1, 6000));
    assert(again.meta.i2c.ack == PROTO_I2C_ACK_NACK && again.payload_length == 0 &&
           again.status == PROTO_EVENT_STATUS_NACK && again.error_code == -1);
    assert(proto_event_validate(&again) == PROTO_EVENT_OK);
    assert(!proto_event_to_i2c(&res, &back));            /* only TX requests convert back */

    txn.op = PROTO_I2C_OP_READ; txn.write_length = 0; txn.read_length = 4;
    assert(proto_event_from_i2c_request(&req, &txn) && req.payload_length == 0);
    assert(proto_event_validate(&req) == PROTO_EVENT_OK);
    txn.address = 0x02;
    assert(!proto_event_from_i2c_request(&req, &txn));
}

static void test_spi(void)
{
    const uint8_t tx[3] = { 0x9F, 0x00, 0x00 };
    proto_event_t req, res;
    assert(proto_event_from_spi_request(&req, tx, 3, 0, 0));
    req.event_id = 9;
    assert(proto_event_validate(&req) == PROTO_EVENT_OK);   /* clock 0 = configured */
    uint8_t out[8]; size_t n = 0;
    assert(proto_event_to_spi(&req, out, sizeof(out), &n) && n == 3 && out[0] == 0x9F);
    const uint8_t rx[3] = { 0xFF, 0xEF, 0x40 };
    assert(proto_event_spi_result(&res, &req, rx, 0, 1000000, PROTO_EVENT_STATUS_OK, 0, 42));
    assert(res.direction == PROTO_DIR_RX && res.correlation_id == 9 && res.payload_length == 3);
    assert(res.meta.spi.clock_hz == 1000000 && res.meta.spi.transaction_length == 3 &&
           res.meta.spi.chip_select == 0 && res.meta.spi.word_bits == 8);
    assert(proto_event_validate(&res) == PROTO_EVENT_OK && res.payload[2] == 0x40);
    assert(proto_event_spi_result(&res, &req, NULL, 0, 1000000, PROTO_EVENT_STATUS_BUS_ERROR, -2, 43));
    assert(res.payload_length == 0 && proto_event_validate(&res) == PROTO_EVENT_OK);
    assert(!proto_event_from_spi_request(&req, tx, 0, 0, 0));
    assert(!proto_event_from_spi_request(&req, tx, 3, 4, 0));
    assert(!proto_event_from_spi_request(&req, tx, 3, 0, 5));
}

static void test_validation_and_loops(void)
{
    proto_event_t ev;
    assert(proto_event_validate(NULL) == PROTO_EVENT_ERR_NULL);
    proto_event_init(&ev, PROTO_PROTOCOL_CAN, PROTO_DIR_RX);
    ev.protocol = PROTO_PROTOCOL_COUNT;
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_PROTOCOL);
    proto_event_init(&ev, PROTO_PROTOCOL_CAN, PROTO_DIR_RX);
    ev.direction = (proto_direction_t)7;
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_DIRECTION);
    proto_event_init(&ev, PROTO_PROTOCOL_CAN, PROTO_DIR_RX);
    ev.status = (proto_event_status_t)9;
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_STATUS);
    proto_event_init(&ev, PROTO_PROTOCOL_CAN, PROTO_DIR_RX);
    ev.payload_length = 9; ev.meta.can.dlc = 8;
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_LENGTH); /* CAN payload max is 8 */
    proto_event_init(&ev, PROTO_PROTOCOL_CAN, PROTO_DIR_RX);
    ev.meta.can.dlc = 2; ev.payload_length = 3;
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_LENGTH);
    proto_event_init(&ev, PROTO_PROTOCOL_CAN, PROTO_DIR_RX);
    ev.hop_count = PROTO_EVENT_MAX_HOPS + 1;
    assert(proto_event_validate(&ev) == PROTO_EVENT_ERR_HOPS);

    proto_can_frame_t f; assert(proto_can_parse_frame("123#01", &f) == PROTO_CAN_PARSE_OK);
    proto_event_t src, out;
    assert(proto_event_from_can(&src, &f, PROTO_DIR_RX));
    src.event_id = 100;
    assert(!proto_event_loop_rejected(&src) && src.hop_count == 0 && !src.from_protoscope);
    assert(proto_event_from_can(&out, &f, PROTO_DIR_TX));
    proto_event_mark_forwarded(&out, &src, 7);
    assert(out.hop_count == 1 && out.origin_route_id == 7 && out.from_protoscope &&
           out.correlation_id == 100);
    assert(proto_event_loop_rejected(&out));              /* own transmission never re-enters */
    src.hop_count = PROTO_EVENT_MAX_HOPS;
    assert(proto_event_loop_rejected(&src));
    assert(proto_event_loop_rejected(NULL));
}

static void test_format(void)
{
    proto_can_frame_t f; proto_event_t ev; char text[400];
    assert(proto_can_parse_frame("123#0105FF", &f) == PROTO_CAN_PARSE_OK);
    assert(proto_event_from_can(&ev, &f, PROTO_DIR_RX));
    ev.event_id = 5;
    assert(proto_event_format(&ev, text, sizeof(text)) > 20);
    assert(strstr(text, "proto=CAN") && strstr(text, "dir=RX") && strstr(text, "data=0105FF") &&
           strstr(text, "id=123 ext=0 rtr=0 dlc=3") && strstr(text, "status=OK"));
    char tiny[8];
    assert(proto_event_format(&ev, tiny, sizeof(tiny)) < sizeof(tiny));
}

int main(void)
{
    test_hex();
    test_caps();
    test_can_roundtrip();
    test_serial_preserves_bytes();
    test_i2c();
    test_spi();
    test_validation_and_loops();
    test_format();
    puts("Common event: hex, capabilities, per-protocol metadata round trips, validation, provenance and text form passed.");
    return 0;
}
