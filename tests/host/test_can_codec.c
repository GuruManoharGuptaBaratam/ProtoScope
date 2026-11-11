#include "proto_can_codec.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_bitrates(void)
{
    assert(proto_can_bitrate_supported(125000) && proto_can_bitrate_supported(250000));
    assert(proto_can_bitrate_supported(500000) && proto_can_bitrate_supported(1000000));
    assert(!proto_can_bitrate_supported(0) && !proto_can_bitrate_supported(100000));
    assert(!proto_can_bitrate_supported(800000));
}

static void test_parse_valid(void)
{
    proto_can_frame_t f;
    assert(proto_can_parse_frame("123#0105FF", &f) == PROTO_CAN_PARSE_OK);
    assert(f.id == 0x123 && !f.extended && !f.rtr && f.dlc == 3);
    assert(f.data[0] == 0x01 && f.data[1] == 0x05 && f.data[2] == 0xFF);
    assert(proto_can_parse_frame("1f334455#aabb", &f) == PROTO_CAN_PARSE_OK);
    assert(f.id == 0x1F334455 && f.extended && f.dlc == 2 && f.data[1] == 0xBB);
    assert(proto_can_parse_frame("7FF#", &f) == PROTO_CAN_PARSE_OK && f.dlc == 0 && !f.rtr);
    assert(proto_can_parse_frame("123#R", &f) == PROTO_CAN_PARSE_OK && f.rtr && f.dlc == 0);
    assert(proto_can_parse_frame("1FFFFFFF#0011223344556677", &f) == PROTO_CAN_PARSE_OK);
    assert(f.dlc == 8 && f.data[7] == 0x77);
}

static void test_parse_invalid(void)
{
    proto_can_frame_t f;
    assert(proto_can_parse_frame(NULL, &f) == PROTO_CAN_PARSE_BAD_FORMAT);
    assert(proto_can_parse_frame("1230105", &f) == PROTO_CAN_PARSE_BAD_FORMAT);
    assert(proto_can_parse_frame("12#01", &f) == PROTO_CAN_PARSE_BAD_ID);
    assert(proto_can_parse_frame("1234#01", &f) == PROTO_CAN_PARSE_BAD_ID);
    assert(proto_can_parse_frame("12G#01", &f) == PROTO_CAN_PARSE_BAD_ID);
    assert(proto_can_parse_frame("20000000#01", &f) == PROTO_CAN_PARSE_BAD_ID);
    assert(proto_can_parse_frame("123#012", &f) == PROTO_CAN_PARSE_BAD_DATA);
    assert(proto_can_parse_frame("123#0G", &f) == PROTO_CAN_PARSE_BAD_DATA);
    assert(proto_can_parse_frame("123#001122334455667788", &f) == PROTO_CAN_PARSE_BAD_DATA);
    assert(proto_can_parse_frame("123#RR", &f) == PROTO_CAN_PARSE_BAD_DATA);
}

static void test_format_roundtrip(void)
{
    const char *cases[] = {"123#0105FF", "1F334455#11", "7FF#", "001#R", "00000001#R",
                           "1FFFFFFF#0011223344556677"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        proto_can_frame_t f;
        char text[PROTO_CAN_TEXT_MAX];
        assert(proto_can_parse_frame(cases[i], &f) == PROTO_CAN_PARSE_OK);
        assert(proto_can_format_frame(&f, text, sizeof(text)) == strlen(cases[i]));
        assert(strcmp(text, cases[i]) == 0);
    }
    proto_can_frame_t f;
    char small[4];
    assert(proto_can_parse_frame("123#01", &f) == PROTO_CAN_PARSE_OK);
    assert(proto_can_format_frame(&f, small, sizeof(small)) == 0);
    f.dlc = 9;
    assert(!proto_can_frame_valid(&f));
    f.dlc = 1; f.id = 0x800;
    assert(!proto_can_frame_valid(&f));
    f.extended = true;
    assert(proto_can_frame_valid(&f));
    f.rtr = true;
    assert(!proto_can_frame_valid(&f));
}

int main(void)
{
    test_bitrates();
    test_parse_valid();
    test_parse_invalid();
    test_format_roundtrip();
    puts("CAN codec: bit rates, parsing, formatting and validation passed.");
    return 0;
}
