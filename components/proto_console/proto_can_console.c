#include "proto_can_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_board.h"
#include "proto_can.h"

static const char *state_name(proto_can_state_t state)
{
    switch (state) {
    case PROTO_CAN_STATE_RUNNING: return "running";
    case PROTO_CAN_STATE_BUS_OFF: return "bus_off";
    case PROTO_CAN_STATE_RECOVERING: return "recovering";
    default: return "stopped";
    }
}

static const char *mode_name(proto_can_mode_t mode)
{
    switch (mode) {
    case PROTO_CAN_MODE_LISTEN_ONLY: return "listen";
    case PROTO_CAN_MODE_SELF_TEST: return "selftest";
    default: return "normal";
    }
}

void proto_can_console_print_status(void)
{
    const proto_can_snapshot_t s = proto_can_get_snapshot();
    printf("can_active=%s bit_rate=%" PRIu32 " tx_gpio=%d rx_gpio=%d\n",
           s.active ? "yes" : "no", s.bit_rate, (int)PROTO_PIN_TWAI_TX, (int)PROTO_PIN_TWAI_RX);
    printf("can_mode=%s can_state=%s tec=%" PRIu32 " rec=%" PRIu32 "\n",
           mode_name(s.mode), state_name(s.state), s.tx_error_counter, s.rx_error_counter);
    printf("rx_frames=%" PRIu32 " tx_requested=%" PRIu32 " tx_completed=%" PRIu32
           " tx_failed=%" PRIu32 " tx_rejected=%" PRIu32 "\n",
           s.rx_frames, s.tx_requested, s.tx_completed, s.tx_failed, s.tx_rejected);
    printf("rx_queue_drops=%" PRIu32 " rx_missed=%" PRIu32 " rx_overrun=%" PRIu32
           " arb_lost=%" PRIu32 " bus_errors=%" PRIu32 "\n",
           s.rx_queue_drops, s.rx_missed, s.rx_overrun, s.arb_lost, s.bus_errors);
    printf("error_passive=%" PRIu32 " bus_off=%" PRIu32 " recoveries=%" PRIu32 "\n",
           s.error_passive, s.bus_off, s.recoveries);
    puts("can_status_end");
}

static int report_error(esp_err_t err)
{
    printf("can_error=%s\n", esp_err_to_name(err));
    return 1;
}

static int can_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_can_console_print_status();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "start") == 0 && argc <= 4) {
        proto_can_config_t config = PROTO_CAN_CONFIG_DEFAULT();
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "normal") == 0) { config.mode = PROTO_CAN_MODE_NORMAL; }
            else if (strcmp(argv[i], "listen") == 0) { config.mode = PROTO_CAN_MODE_LISTEN_ONLY; }
            else if (strcmp(argv[i], "selftest") == 0) { config.mode = PROTO_CAN_MODE_SELF_TEST; }
            else {
                char *end = NULL;
                const unsigned long rate = strtoul(argv[i], &end, 10);
                if (end == argv[i] || *end != '\0' || !proto_can_bitrate_supported((uint32_t)rate)) {
                    return report_error(ESP_ERR_INVALID_ARG);
                }
                config.bit_rate = (uint32_t)rate;
            }
        }
        const esp_err_t err = proto_can_start(&config);
        if (err != ESP_OK) { return report_error(err); }
        printf("can_start=ok bit_rate=%" PRIu32 " mode=%s\n", config.bit_rate,
               mode_name(config.mode));
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "send") == 0) {
        proto_can_frame_t frame;
        if (proto_can_parse_frame(argv[2], &frame) != PROTO_CAN_PARSE_OK) {
            return report_error(ESP_ERR_INVALID_ARG);
        }
        const esp_err_t err = proto_can_send(&frame);
        if (err != ESP_OK) { return report_error(err); }
        printf("can_tx=queued frame=%s\n", argv[2]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "read") == 0) {
        proto_can_frame_t frame;
        const esp_err_t err = proto_can_receive(&frame);
        if (err == ESP_ERR_NOT_FOUND) { puts("can_rx=empty"); return 0; }
        if (err != ESP_OK) { return report_error(err); }
        char text[PROTO_CAN_TEXT_MAX];
        if (proto_can_format_frame(&frame, text, sizeof(text)) == 0) {
            return report_error(ESP_FAIL);
        }
        printf("can_rx frame=%s time_us=%" PRIu64 "\n", text, frame.timestamp_us);
        return 0;
    }
    puts("Usage: can start [125000|250000|500000|1000000] [normal|listen|selftest]");
    puts("       can status | can send ID#HEX (e.g. 123#0105FF, 1F334455#11, 123#R) | can read");
    puts("CAN starts only on request. Reboot to stop or change its configuration.");
    return 1;
}

esp_err_t proto_can_console_register(void)
{
    const esp_console_cmd_t command = {
        .command = "can",
        .help = "Phase 3 CAN (TWAI): start [bitrate] [mode], status, send ID#HEX, read",
        .func = can_command,
    };
    return esp_console_cmd_register(&command);
}
