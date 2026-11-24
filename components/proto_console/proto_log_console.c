#include "proto_log_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_event_caps.h"
#include "proto_monitor.h"
#include "proto_storage.h"

void proto_log_console_print_status(void)
{
    const proto_storage_snapshot_t s = proto_storage_get_snapshot();
    printf("log_state=%s wanted=%s card=%s file=%s monitor_mode=%s mask=0x%02" PRIX32 "\n",
           proto_logger_state_name(s.state), s.wanted ? "yes" : "no", s.card_mounted ? "mounted" : "none",
           s.file_name[0] ? s.file_name : "-", s.monitor_mode ? "on" : "off", s.protocol_mask);
    printf("records=%" PRIu32 " file_bytes=%" PRIu32 " files=%" PRIu32 " write_errors=%" PRIu32
           " dropped_total=%" PRIu32 " dropped_observer=%" PRIu32 " queue=%u high_water=%u\n",
           s.records_written, s.file_bytes, s.files_created, s.write_errors, s.dropped_total,
           s.dropped_observer, (unsigned)s.queue_depth, (unsigned)s.queue_high_water);
    puts("log_status_end");
}

static int log_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_log_console_print_status();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "start") == 0) {
        const proto_log_state_t state = proto_storage_log_start();
        printf("log_start=%s\n", proto_logger_state_name(state));
        return state == PROTO_LOG_STATE_RUNNING ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        proto_storage_log_stop();
        puts("log_stop=ok");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "monitor") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        proto_storage_set_monitor_mode(strcmp(argv[2], "on") == 0);
        printf("log_monitor=%s\n", argv[2]);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "filter") == 0) {
        uint32_t mask = 0;
        for (int i = 2; i < argc; ++i) {
            proto_protocol_t p;
            if (strcmp(argv[i], "all") == 0) { mask = PROTO_LOG_MASK_ALL; continue; }
            if (!proto_protocol_parse(argv[i], &p)) { puts("log_error=unknown_protocol"); return 1; }
            mask |= PROTO_LOG_MASK(p);
        }
        proto_storage_set_protocol_mask(mask);
        printf("log_filter=ok mask=0x%02" PRIX32 "\n", mask);
        return 0;
    }
    puts("Usage: log start | log stop | log status | log filter all|uart|rs485|can|i2c|spi ...");
    puts("       log monitor on|off   (also drain interfaces that no enabled route uses)");
    puts("Needs a FAT-formatted microSD card on the UI SPI bus. Files: LOG0001.CSV, LOG0002.CSV, ...");
    return 1;
}

esp_err_t proto_log_console_register(void)
{
    const esp_err_t err = proto_storage_start();
    if (err != ESP_OK) { return err; }
    const esp_console_cmd_t command = {
        .command = "log",
        .help = "Phase 8 microSD logger: start, stop, status, filter, monitor on|off",
        .func = log_command,
    };
    return esp_console_cmd_register(&command);
}
