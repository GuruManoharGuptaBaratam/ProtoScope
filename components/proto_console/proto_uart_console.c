#include "proto_uart_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "proto_board.h"
#include "proto_uart.h"

void proto_uart_console_print_status(void)
{
    const proto_uart_snapshot_t s = proto_uart_get_snapshot();
    printf("uart_active=%s baud=%d data=8 parity=N stop=1 tx_gpio=%d rx_gpio=%d\n",
           s.active ? "yes" : "no", s.baud_rate,
           (int)PROTO_PIN_UART_TX, (int)PROTO_PIN_UART_RX);
    printf("rx_bytes=%" PRIu32 " rx_frames=%" PRIu32 " tx_bytes=%" PRIu32
           " tx_completed=%" PRIu32 "\n", s.rx_bytes, s.rx_frames, s.tx_bytes, s.tx_completed);
    printf("malformed=%" PRIu32 " overlong=%" PRIu32 " partial_timeouts=%" PRIu32
           " rx_queue_drops=%" PRIu32 "\n",
           s.malformed, s.overlong, s.partial_timeouts, s.rx_queue_drops);
    printf("fifo_overflows=%" PRIu32 " buffer_full=%" PRIu32 " frame_errors=%" PRIu32
           " parity_errors=%" PRIu32 " breaks=%" PRIu32 " tx_failures=%" PRIu32 "\n",
           s.fifo_overflows, s.buffer_full, s.frame_errors, s.parity_errors, s.breaks, s.tx_failures);
    puts("uart_status_end");
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
    printf("uart_error=%s\n", esp_err_to_name(err));
    return 1;
}

static int uart_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_uart_console_print_status();
        return 0;
    }
    if ((argc == 2 || argc == 3) && strcmp(argv[1], "start") == 0) {
        proto_uart_config_t config = PROTO_UART_CONFIG_DEFAULT();
        if (argc == 3) {
            if (strcmp(argv[2], "9600") == 0) { config.baud_rate = 9600; }
            else if (strcmp(argv[2], "115200") != 0) {
                return report_error(ESP_ERR_INVALID_ARG);
            }
        }
        const esp_err_t err = proto_uart_start(&config);
        if (err != ESP_OK) { return report_error(err); }
        printf("uart_start=ok baud=%d data=8 parity=N stop=1\n", config.baud_rate);
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
        const esp_err_t err = proto_uart_send(bytes, hex_length / 2);
        if (err != ESP_OK) { return report_error(err); }
        printf("uart_tx=ok bytes=%u\n", (unsigned)(hex_length / 2));
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "read") == 0) {
        proto_uart_frame_t frame;
        const esp_err_t err = proto_uart_receive(&frame);
        if (err == ESP_ERR_NOT_FOUND) { puts("uart_rx=empty"); return 0; }
        if (err != ESP_OK) { return report_error(err); }
        printf("uart_rx bytes=%u hex=", (unsigned)frame.length);
        for (size_t i = 0; i < frame.length; ++i) { printf("%02X", frame.data[i]); }
        printf(" time_us=%" PRIu64 "\n", frame.completed_us);
        return 0;
    }
    puts("Usage: uart start [9600|115200] | uart status | uart send HEX | uart read");
    puts("UART starts only on request. Reboot to stop or change its baud rate.");
    return 1;
}

esp_err_t proto_uart_console_register(void)
{
    const esp_console_cmd_t command = {
        .command = "uart",
        .help = "Phase 2 UART1: start [9600|115200], status, send HEX, read",
        .func = uart_command,
    };
    return esp_console_cmd_register(&command);
}
