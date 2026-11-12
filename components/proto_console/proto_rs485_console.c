#include "proto_rs485_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_board.h"
#include "proto_rs485.h"

void proto_rs485_console_print_status(void)
{
    const proto_rs485_snapshot_t s = proto_rs485_get_snapshot();
    printf("rs485_active=%s baud=%" PRIu32 " data=8 parity=N stop=1 tx_gpio=%d rx_gpio=%d"
           " echo_discard=%s guard_us=%" PRIu32 "\n",
           s.active ? "yes" : "no", s.baud_rate, (int)PROTO_PIN_RS485_TX,
           (int)PROTO_PIN_RS485_RX, s.discard_echo ? "yes" : "no", s.guard_us);
    printf("rx_bytes=%" PRIu32 " rx_frames=%" PRIu32 " tx_bytes=%" PRIu32
           " tx_completed=%" PRIu32 " echo_bytes=%" PRIu32 "\n",
           s.rx_bytes, s.rx_frames, s.tx_bytes, s.tx_completed, s.echo_bytes);
    printf("malformed=%" PRIu32 " overlong=%" PRIu32 " partial_timeouts=%" PRIu32
           " rx_queue_drops=%" PRIu32 "\n",
           s.malformed, s.overlong, s.partial_timeouts, s.rx_queue_drops);
    printf("fifo_overflows=%" PRIu32 " buffer_full=%" PRIu32 " frame_errors=%" PRIu32
           " parity_errors=%" PRIu32 " breaks=%" PRIu32 " tx_failures=%" PRIu32 "\n",
           s.fifo_overflows, s.buffer_full, s.frame_errors, s.parity_errors, s.breaks,
           s.tx_failures);
    puts("rs485_status_end");
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

static int report_error(esp_err_t err)
{
    printf("rs485_error=%s\n", esp_err_to_name(err));
    return 1;
}

static int rs485_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_rs485_console_print_status();
        return 0;
    }
    if (argc >= 2 && argc <= 4 && strcmp(argv[1], "start") == 0) {
        proto_rs485_config_t config = PROTO_RS485_CONFIG_DEFAULT();
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "keepecho") == 0) {
                config.discard_echo = false;
            } else {
                char *end = NULL;
                const unsigned long baud = strtoul(argv[i], &end, 10);
                if (end == argv[i] || *end != '\0' || !proto_rs485_baud_supported((uint32_t)baud)) {
                    return report_error(ESP_ERR_INVALID_ARG);
                }
                config.baud_rate = (uint32_t)baud;
            }
        }
        const esp_err_t err = proto_rs485_start(&config);
        if (err != ESP_OK) { return report_error(err); }
        printf("rs485_start=ok baud=%" PRIu32 " data=8 parity=N stop=1 echo_discard=%s\n",
               config.baud_rate, config.discard_echo ? "yes" : "no");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "send") == 0) {
        const size_t hex_length = strlen(argv[2]);
        if (hex_length == 0 || hex_length % 2 || hex_length > 2 * PROTO_UART_FRAME_MAX) {
            return report_error(ESP_ERR_INVALID_ARG);
        }
        uint8_t bytes[PROTO_UART_FRAME_MAX];
        for (size_t i = 0; i < hex_length; i += 2) {
            const int high = hex_nibble(argv[2][i]);
            const int low = hex_nibble(argv[2][i + 1]);
            if (high < 0 || low < 0) { return report_error(ESP_ERR_INVALID_ARG); }
            bytes[i / 2] = (uint8_t)((high << 4) | low);
        }
        const esp_err_t err = proto_rs485_send(bytes, hex_length / 2);
        if (err != ESP_OK) { return report_error(err); }
        printf("rs485_tx=ok bytes=%u\n", (unsigned)(hex_length / 2));
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "read") == 0) {
        proto_uart_frame_t frame;
        const esp_err_t err = proto_rs485_receive(&frame);
        if (err == ESP_ERR_NOT_FOUND) { puts("rs485_rx=empty"); return 0; }
        if (err != ESP_OK) { return report_error(err); }
        printf("rs485_rx bytes=%u hex=", (unsigned)frame.length);
        for (size_t i = 0; i < frame.length; ++i) { printf("%02X", frame.data[i]); }
        printf(" time_us=%" PRIu64 "\n", frame.completed_us);
        return 0;
    }
    puts("Usage: rs485 start [9600|19200|38400|57600|115200] [keepecho]");
    puts("       rs485 status | rs485 send HEX | rs485 read");
    puts("RS-485 starts only on request. Reboot to stop or change settings.");
    puts("keepecho: do not discard bytes received during our own transmit (to measure echo).");
    return 1;
}

esp_err_t proto_rs485_console_register(void)
{
    const esp_console_cmd_t command = {
        .command = "rs485",
        .help = "Phase 4 RS-485 (UART2): start [baud] [keepecho], status, send HEX, read",
        .func = rs485_command,
    };
    return esp_console_cmd_register(&command);
}
