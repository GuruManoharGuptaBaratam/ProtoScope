#include "proto_spi_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_board.h"
#include "proto_hex.h"
#include "proto_spi.h"

void proto_spi_console_print_status(void)
{
    const proto_spi_snapshot_t s = proto_spi_get_snapshot();
    printf("spi_active=%s role=controller mode=%u clock_hz=%" PRIu32
           " sclk_gpio=%d mosi_gpio=%d miso_gpio=%d cs0_gpio=%d\n",
           s.active ? "yes" : "no", (unsigned)s.mode, s.clock_hz, (int)PROTO_PIN_EXT_SPI_SCLK,
           (int)PROTO_PIN_EXT_SPI_MOSI, (int)PROTO_PIN_EXT_SPI_MISO, (int)PROTO_PIN_EXT_SPI_CS0);
    printf("transfers=%" PRIu32 " completed=%" PRIu32 " failures=%" PRIu32 " rejected=%" PRIu32
           " bytes=%" PRIu32 " last_error=%s\n", s.transfers, s.completed, s.failures,
           s.rejected, s.bytes, esp_err_to_name(s.last_error));
    puts("spi_status_end");
}

static int report_error(esp_err_t err)
{
    printf("spi_error=%s\n", esp_err_to_name(err));
    return 1;
}

static int spi_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_spi_console_print_status();
        return 0;
    }
    if (argc >= 2 && argc <= 4 && strcmp(argv[1], "start") == 0) {
        proto_spi_config_t config = PROTO_SPI_CONFIG_DEFAULT();
        if (argc >= 3) {
            char *end = NULL;
            const unsigned long mode = strtoul(argv[2], &end, 10);
            if (end == argv[2] || *end != '\0' || mode > 3) { return report_error(ESP_ERR_INVALID_ARG); }
            config.mode = (uint8_t)mode;
        }
        if (argc == 4) {
            char *end = NULL;
            const unsigned long clock = strtoul(argv[3], &end, 10);
            if (end == argv[3] || *end != '\0') { return report_error(ESP_ERR_INVALID_ARG); }
            config.clock_hz = (uint32_t)clock;
        }
        const esp_err_t err = proto_spi_start(&config);
        if (err != ESP_OK) { return report_error(err); }
        printf("spi_start=ok role=controller mode=%u clock_hz=%" PRIu32 "\n",
               (unsigned)config.mode, config.clock_hz);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "xfer") == 0) {
        uint8_t tx[PROTO_SPI_MAX_TRANSFER], rx[PROTO_SPI_MAX_TRANSFER];
        size_t length = 0;
        if (!proto_hex_parse(argv[2], tx, sizeof(tx), &length)) {
            return report_error(ESP_ERR_INVALID_ARG);
        }
        const esp_err_t err = proto_spi_transfer(tx, rx, length);
        if (err != ESP_OK) { return report_error(err); }
        char hex[2 * PROTO_SPI_MAX_TRANSFER + 1];
        (void)proto_hex_format(rx, length, hex, sizeof(hex));
        printf("spi_rx bytes=%u hex=%s\n", (unsigned)length, hex);
        return 0;
    }
    puts("Usage: spi start [MODE 0-3] [CLOCK_HZ 1000-10000000] | spi status | spi xfer HEX");
    puts("Full duplex, CS0 asserted for the whole transfer (1..64 bytes). Controller only.");
    puts("SPI starts only on request. Reboot to stop or change settings.");
    return 1;
}

esp_err_t proto_spi_console_register(void)
{
    const esp_console_cmd_t command = {
        .command = "spi",
        .help = "Phase 5 SPI controller: start [mode] [clock_hz], status, xfer HEX",
        .func = spi_command,
    };
    return esp_console_cmd_register(&command);
}
