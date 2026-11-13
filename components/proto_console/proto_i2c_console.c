#include "proto_i2c_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_board.h"
#include "proto_hex.h"
#include "proto_i2c.h"

void proto_i2c_console_print_status(void)
{
    const proto_i2c_snapshot_t s = proto_i2c_get_snapshot();
    printf("i2c_active=%s role=controller freq_hz=%" PRIu32 " sda_gpio=%d scl_gpio=%d"
           " internal_pullup=%s\n", s.active ? "yes" : "no", s.frequency_hz,
           (int)PROTO_PIN_I2C_SDA, (int)PROTO_PIN_I2C_SCL, s.internal_pullup ? "yes" : "no");
    printf("probes=%" PRIu32 " devices_found=%" PRIu32 " transactions=%" PRIu32
           " completed=%" PRIu32 "\n", s.probes, s.devices_found, s.transactions, s.completed);
    printf("nacks=%" PRIu32 " timeouts=%" PRIu32 " bus_errors=%" PRIu32 " rejected=%" PRIu32
           " last_error=%s\n", s.nacks, s.timeouts, s.bus_errors, s.rejected,
           esp_err_to_name(s.last_error));
    puts("i2c_status_end");
}

static int report_error(esp_err_t err)
{
    printf("i2c_error=%s\n", esp_err_to_name(err));
    return 1;
}

static bool parse_number(const char *text, int base, unsigned long max, unsigned long *value)
{
    char *end = NULL;
    const unsigned long v = strtoul(text, &end, base);
    if (end == text || *end != '\0' || v > max) { return false; }
    *value = v;
    return true;
}

static int i2c_command(int argc, char **argv)
{
    unsigned long address = 0, length = 0;
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_i2c_console_print_status();
        return 0;
    }
    if (argc >= 2 && argc <= 4 && strcmp(argv[1], "start") == 0) {
        proto_i2c_config_t config = PROTO_I2C_CONFIG_DEFAULT();
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "pullup") == 0) {
                config.internal_pullup = true;
            } else if (parse_number(argv[i], 10, 1000000, &length) &&
                       proto_i2c_frequency_supported((uint32_t)length)) {
                config.frequency_hz = (uint32_t)length;
            } else {
                return report_error(ESP_ERR_INVALID_ARG);
            }
        }
        const esp_err_t err = proto_i2c_start(&config);
        if (err != ESP_OK) { return report_error(err); }
        printf("i2c_start=ok role=controller freq_hz=%" PRIu32 " internal_pullup=%s\n",
               config.frequency_hz, config.internal_pullup ? "yes" : "no");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "scan") == 0) {
        unsigned count = 0;
        for (unsigned a = PROTO_I2C_ADDR_MIN; a <= PROTO_I2C_ADDR_MAX; ++a) {
            const esp_err_t err = proto_i2c_probe((uint8_t)a);
            if (err == ESP_OK) {
                printf("i2c_found=0x%02X\n", a);
                ++count;
            } else if (err == ESP_ERR_INVALID_STATE) {
                return report_error(err);
            }
        }
        printf("i2c_scan_done count=%u\n", count);
        return 0;
    }
    proto_i2c_txn_t txn;
    memset(&txn, 0, sizeof(txn));
    uint8_t read_data[PROTO_I2C_MAX_TRANSFER];
    size_t hex_length = 0;
    if (argc == 4 && strcmp(argv[1], "write") == 0 && parse_number(argv[2], 16, 0x7F, &address) &&
        proto_hex_parse(argv[3], txn.write_data, sizeof(txn.write_data), &hex_length)) {
        txn.address = (uint8_t)address; txn.op = PROTO_I2C_OP_WRITE;
        txn.write_length = (uint8_t)hex_length;
    } else if (argc == 4 && strcmp(argv[1], "read") == 0 &&
               parse_number(argv[2], 16, 0x7F, &address) && parse_number(argv[3], 10, 64, &length)) {
        txn.address = (uint8_t)address; txn.op = PROTO_I2C_OP_READ;
        txn.read_length = (uint8_t)length;
    } else if (argc == 5 && strcmp(argv[1], "wr") == 0 && parse_number(argv[2], 16, 0x7F, &address) &&
               proto_hex_parse(argv[3], txn.write_data, sizeof(txn.write_data), &hex_length) &&
               parse_number(argv[4], 10, 64, &length)) {
        txn.address = (uint8_t)address; txn.op = PROTO_I2C_OP_WRITE_READ;
        txn.write_length = (uint8_t)hex_length; txn.read_length = (uint8_t)length;
    } else {
        puts("Usage: i2c start [100000|400000] [pullup] | i2c status | i2c scan");
        puts("       i2c write ADDR HEX | i2c read ADDR LEN | i2c wr ADDR HEX LEN");
        puts("ADDR is a 7-bit hex address (e.g. 68 or 0x68). ProtoScope is controller only.");
        puts("I2C starts only on request. Reboot to stop or change settings.");
        return 1;
    }
    const esp_err_t err = proto_i2c_transact(&txn, read_data);
    if (err != ESP_OK) { return report_error(err); }
    if (txn.read_length) {
        char hex[2 * PROTO_I2C_MAX_TRANSFER + 1];
        (void)proto_hex_format(read_data, txn.read_length, hex, sizeof(hex));
        printf("i2c_rx addr=0x%02X bytes=%u hex=%s\n", txn.address, (unsigned)txn.read_length, hex);
    } else {
        printf("i2c_tx=ok addr=0x%02X bytes=%u\n", txn.address, (unsigned)txn.write_length);
    }
    return 0;
}

esp_err_t proto_i2c_console_register(void)
{
    const esp_console_cmd_t command = {
        .command = "i2c",
        .help = "Phase 5 I2C controller: start [freq] [pullup], status, scan, write, read, wr",
        .func = i2c_command,
    };
    return esp_console_cmd_register(&command);
}
