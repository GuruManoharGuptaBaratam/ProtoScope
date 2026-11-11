#include "proto_uart_framer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static proto_uart_frame_result_t feed(proto_uart_framer_t *framer, const char *bytes,
                                      size_t length, uint64_t now, proto_uart_frame_t *frame)
{
    proto_uart_frame_result_t result = PROTO_UART_FRAME_PENDING;
    for (size_t i = 0; i < length; ++i) {
        result = proto_uart_framer_feed(framer, (uint8_t)bytes[i], now, frame);
    }
    return result;
}

static void test_fragmentation_and_raw_bytes(void)
{
    proto_uart_framer_t framer = {0};
    proto_uart_frame_t frame = {0};
    assert(feed(&framer, "HEL", 3, 1000, &frame) == PROTO_UART_FRAME_PENDING);
    assert(feed(&framer, "LO\r\n", 4, 2000, &frame) == PROTO_UART_FRAME_READY);
    assert(frame.length == 7 && memcmp(frame.data, "HELLO\r\n", 7) == 0);
    assert(frame.completed_us == 2000);
    assert(feed(&framer, "X\n", 2, 3000, &frame) == PROTO_UART_FRAME_READY);
    assert(frame.length == 2 && memcmp(frame.data, "X\n", 2) == 0);
}

static void test_boundaries_and_recovery(void)
{
    proto_uart_framer_t framer = {0};
    proto_uart_frame_t frame;
    char bytes[64];
    memset(bytes, 'a', sizeof(bytes));
    assert(feed(&framer, bytes, 63, 0, &frame) == PROTO_UART_FRAME_PENDING);
    assert(feed(&framer, "\n", 1, 0, &frame) == PROTO_UART_FRAME_READY);
    assert(frame.length == 64);
    assert(feed(&framer, bytes, 64, 0, &frame) == PROTO_UART_FRAME_TOO_LONG);
    assert(feed(&framer, "tail\n", 5, 0, &frame) == PROTO_UART_FRAME_PENDING);
    assert(feed(&framer, "OK\n", 3, 0, &frame) == PROTO_UART_FRAME_READY);
    assert(frame.length == 3);
    assert(feed(&framer, bytes, 62, 0, &frame) == PROTO_UART_FRAME_PENDING);
    assert(feed(&framer, "\r\n", 2, 0, &frame) == PROTO_UART_FRAME_READY);
    assert(frame.length == 64);
}

static void test_invalid_lines(void)
{
    proto_uart_framer_t framer = {0};
    proto_uart_frame_t frame;
    assert(feed(&framer, "\n", 1, 0, &frame) == PROTO_UART_FRAME_MALFORMED);
    assert(feed(&framer, "\r\n", 2, 0, &frame) == PROTO_UART_FRAME_MALFORMED);
    assert(feed(&framer, "ab\rX", 4, 0, &frame) == PROTO_UART_FRAME_MALFORMED);
    assert(feed(&framer, "ignored\n", 8, 0, &frame) == PROTO_UART_FRAME_PENDING);
    const char nul = 0;
    assert(feed(&framer, &nul, 1, 0, &frame) == PROTO_UART_FRAME_MALFORMED);
    assert(feed(&framer, "\n", 1, 0, &frame) == PROTO_UART_FRAME_PENDING);
    const char non_ascii = (char)0xff;
    assert(feed(&framer, &non_ascii, 1, 0, &frame) == PROTO_UART_FRAME_MALFORMED);
    assert(feed(&framer, "\n", 1, 0, &frame) == PROTO_UART_FRAME_PENDING);
    assert(feed(&framer, "OK\n", 3, 0, &frame) == PROTO_UART_FRAME_READY);
}

static void test_timeout_and_loss(void)
{
    proto_uart_framer_t framer = {0};
    proto_uart_frame_t frame;
    assert(!proto_uart_framer_expire(&framer, 900000));
    feed(&framer, "part", 4, 1000000, &frame);
    assert(!proto_uart_framer_expire(&framer, 1199999));
    assert(proto_uart_framer_expire(&framer, 1200000));
    assert(!proto_uart_framer_expire(&framer, 1400000));
    assert(feed(&framer, "NEW\n", 4, 1400000, &frame) == PROTO_UART_FRAME_READY);
    assert(frame.length == 4);
    feed(&framer, "partial", 7, 1400000, &frame);
    proto_uart_framer_resync(&framer, 1400000);
    assert(feed(&framer, "tail\n", 5, 1400000, &frame) == PROTO_UART_FRAME_PENDING);
    assert(feed(&framer, "OK\n", 3, 1400000, &frame) == PROTO_UART_FRAME_READY);
    proto_uart_framer_resync(&framer, 1400000);
    assert(!proto_uart_framer_expire(&framer, 1600000));
    assert(!framer.discarding);
    assert(feed(&framer, "OK\n", 3, 1600000, &frame) == PROTO_UART_FRAME_READY);
}

static void test_every_split_and_byte(void)
{
    const char line[] = "CAN,123,3,0105FF\r\n";
    for (size_t split = 0; split < sizeof(line) - 1; ++split) {
        proto_uart_framer_t framer = {0};
        proto_uart_frame_t frame;
        assert(feed(&framer, line, split, 1, &frame) == PROTO_UART_FRAME_PENDING);
        assert(feed(&framer, line + split, sizeof(line) - 1 - split, 2, &frame) ==
               PROTO_UART_FRAME_READY);
        assert(frame.length == sizeof(line) - 1);
        assert(memcmp(frame.data, line, frame.length) == 0);
    }
    for (unsigned byte = 0; byte <= 255; ++byte) {
        proto_uart_framer_t framer = {0};
        proto_uart_frame_t frame;
        proto_uart_framer_feed(&framer, (uint8_t)byte, 0, &frame);
        const proto_uart_frame_result_t result = proto_uart_framer_feed(&framer, '\n', 0, &frame);
        assert((result == PROTO_UART_FRAME_READY) == (byte >= 0x20 && byte <= 0x7e));
        assert(framer.length < PROTO_UART_FRAME_MAX);
    }
}

int main(void)
{
    test_fragmentation_and_raw_bytes();
    test_boundaries_and_recovery();
    test_invalid_lines();
    test_timeout_and_loss();
    test_every_split_and_byte();
    puts("UART framing: fragmentation, boundaries, malformed input, timeouts and recovery passed.");
    return 0;
}
