#include "proto_msg_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_driver.h"
#include "proto_event.h"
#include "proto_event_caps.h"
#include "proto_hex.h"
#include "proto_spi.h"

static int report_error(esp_err_t err)
{
    printf("msg_error=%s\n", esp_err_to_name(err));
    return 1;
}

static void print_event(const char *label, const proto_event_t *event)
{
    char text[480];
    (void)proto_event_format(event, text, sizeof(text));
    printf("%s %s\n", label, text);
}

static bool parse_number(const char *text, int base, unsigned long max, unsigned long *value)
{
    char *end = NULL;
    const unsigned long v = strtoul(text, &end, base);
    if (end == text || *end != '\0' || v > max) { return false; }
    *value = v;
    return true;
}

static void print_caps(void)
{
    for (int i = 0; i < PROTO_PROTOCOL_COUNT; ++i) {
        const proto_driver_t *driver = proto_driver_get((proto_protocol_t)i);
        char caps[96];
        (void)proto_caps_format(driver->caps, caps, sizeof(caps));
        printf("msg_caps protocol=%s active=%s max_payload=%u receive=%s caps=%s\n", driver->name,
               driver->is_active() ? "yes" : "no", (unsigned)driver->max_payload,
               driver->receive ? "events" : "controller_transaction", caps);
    }
    puts("msg_caps_end");
}

/* Builds a TX request event from the console arguments. */
static bool build_request(proto_protocol_t protocol, int argc, char **argv, proto_event_t *event)
{
    switch (protocol) {
    case PROTO_PROTOCOL_UART:
    case PROTO_PROTOCOL_RS485: {
        proto_uart_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        return argc == 4 && proto_hex_parse(argv[3], frame.data, sizeof(frame.data), &frame.length) &&
               proto_event_from_serial(event, protocol, &frame, 0, PROTO_DIR_TX);
    }
    case PROTO_PROTOCOL_CAN: {
        proto_can_frame_t frame;
        return argc == 4 && proto_can_parse_frame(argv[3], &frame) == PROTO_CAN_PARSE_OK &&
               proto_event_from_can(event, &frame, PROTO_DIR_TX);
    }
    case PROTO_PROTOCOL_SPI: {
        uint8_t tx[PROTO_SPI_MAX_TRANSFER];
        size_t length = 0;
        const proto_spi_snapshot_t spi = proto_spi_get_snapshot();
        return argc == 4 && proto_hex_parse(argv[3], tx, sizeof(tx), &length) &&
               proto_event_from_spi_request(event, tx, length, spi.mode, spi.clock_hz);
    }
    case PROTO_PROTOCOL_I2C: {
        proto_i2c_txn_t txn;
        memset(&txn, 0, sizeof(txn));
        unsigned long address = 0, length = 0;
        size_t hex_length = 0;
        if (argc < 6 || !parse_number(argv[3], 16, 0x7F, &address)) { return false; }
        txn.address = (uint8_t)address;
        if (argc == 6 && strcmp(argv[4], "w") == 0 &&
            proto_hex_parse(argv[5], txn.write_data, sizeof(txn.write_data), &hex_length)) {
            txn.op = PROTO_I2C_OP_WRITE; txn.write_length = (uint8_t)hex_length;
        } else if (argc == 6 && strcmp(argv[4], "r") == 0 && parse_number(argv[5], 10, 64, &length)) {
            txn.op = PROTO_I2C_OP_READ; txn.read_length = (uint8_t)length;
        } else if (argc == 7 && strcmp(argv[4], "wr") == 0 &&
                   proto_hex_parse(argv[5], txn.write_data, sizeof(txn.write_data), &hex_length) &&
                   parse_number(argv[6], 10, 64, &length)) {
            txn.op = PROTO_I2C_OP_WRITE_READ; txn.write_length = (uint8_t)hex_length;
            txn.read_length = (uint8_t)length;
        } else {
            return false;
        }
        return proto_event_from_i2c_request(event, &txn);
    }
    case PROTO_PROTOCOL_COUNT:
        break;
    }
    return false;
}

static int msg_command(int argc, char **argv)
{
    proto_protocol_t protocol;
    if (argc == 2 && strcmp(argv[1], "caps") == 0) {
        print_caps();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "poll") == 0 && proto_protocol_parse(argv[2], &protocol)) {
        const proto_driver_t *driver = proto_driver_get(protocol);
        if (!driver->receive) {
            printf("msg_error=%s reason=controller_only_use_msg_send\n",
                   esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
            return 1;
        }
        proto_event_t event;
        const esp_err_t err = driver->receive(&event);
        if (err == ESP_ERR_NOT_FOUND) { puts("msg_rx=empty"); return 0; }
        if (err != ESP_OK) { return report_error(err); }
        print_event("msg_rx", &event);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "send") == 0 && proto_protocol_parse(argv[2], &protocol)) {
        const proto_driver_t *driver = proto_driver_get(protocol);
        proto_event_t request, result;
        if (!build_request(protocol, argc, argv, &request)) { return report_error(ESP_ERR_INVALID_ARG); }
        request.event_id = proto_event_next_id();
        memset(&result, 0, sizeof(result));
        const esp_err_t err = driver->submit(&request, &result);
        print_event("msg_request", &request);
        print_event(err == ESP_OK ? "msg_result" : "msg_failed", &result);
        return err == ESP_OK ? 0 : 1;
    }
    puts("Usage: msg caps | msg poll uart|rs485|can");
    puts("       msg send uart|rs485 HEX | msg send can ID#HEX | msg send spi HEX");
    puts("       msg send i2c ADDR w HEX | msg send i2c ADDR r LEN | msg send i2c ADDR wr HEX LEN");
    puts("Uses the common event envelope and driver contract. Start the interface first.");
    return 1;
}

esp_err_t proto_msg_console_register(void)
{
    const esp_console_cmd_t command = {
        .command = "msg",
        .help = "Phase 6 common events: caps, poll PROTO, send PROTO ...",
        .func = msg_command,
    };
    return esp_console_cmd_register(&command);
}
