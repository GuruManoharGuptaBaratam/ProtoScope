/*
 * ProtoScope TEMPORARY hardware self-test (see proto_selftest.h).
 *
 *   protoscope> selftest            run every test in order
 *   protoscope> selftest <name>     run one: uart can rs485 i2c oled spi sd enc
 *
 * Run it straight after a reboot, before any `uart/can/rs485/i2c/spi start`
 * or `log start`, so this test picks the interface settings.
 *
 * How each test proves the hardware (no simulated values anywhere):
 *   uart  : random lines out of GPIO17, must come back on GPIO18 (jumper 17->18).
 *   can   : TWAI self-test mode (no ACK needed). The controller transmits through
 *           the SN65HVD230 and must read the same frame back through its RX pin.
 *   rs485 : random line out through the module. PASS if the module echoes it back
 *           (receiver live while driving A/B) or if a second RS-485 node sends a line.
 *   i2c   : scans 0x08-0x77 on GPIO8/GPIO9 and reads an ID register where known.
 *   oled  : I2C1 GPIO14/GPIO10: reads the address the SSD1306 ACKed and checks
 *           that screen updates are being acknowledged, shows a test pattern.
 *   spi   : random bytes out of MOSI GPIO40, compared on MISO GPIO41 (jumper 40->41);
 *           if a device is fitted instead, reads a JEDEC ID (0x9F).
 *   sd    : mounts the card on SPI2 (CS GPIO7), prints the card's own registers,
 *           writes /SELFTEST.TXT with random data, reads it back and compares.
 *   enc   : counts real detents and presses from GPIO1/GPIO2/GPIO6 for 10 s.
 */
#include "proto_selftest.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_sdmmc.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "proto_board.h"
#include "proto_can.h"
#include "proto_display.h"
#include "proto_encoder.h"
#include "proto_i2c.h"
#include "proto_rs485.h"
#include "proto_spi.h"
#include "proto_storage.h"
#include "proto_uart.h"
#include "proto_ui.h"
#include "proto_uibus.h"

#define ST_SD_MOUNT "/sdst"
#define ST_SD_FILE ST_SD_MOUNT "/SELFTEST.TXT"
#define ST_SD_BYTES 8192
#define ST_DETAIL 120

typedef enum { ST_NOT_RUN = 0, ST_PASS, ST_FAIL, ST_SKIP } st_status_t;

typedef struct {
    const char *name;
    st_status_t (*run)(char *detail);
    bool only_on_request;   /* not part of `selftest` / `selftest all` */
    st_status_t status;
    char detail[ST_DETAIL];
} st_test_t;

static int64_t now_us(void) { return esp_timer_get_time(); }

static void detail_set(char *detail, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void detail_set(char *detail, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, ST_DETAIL, fmt, ap);
    va_end(ap);
}

static void print_hex(const char *label, const uint8_t *data, size_t len)
{
    printf("    %s:", label);
    for (size_t i = 0; i < len; ++i) { printf(" %02X", data[i]); }
    printf("\n");
}

/* Printable random line: "<tag>-<seq>-<24 random hex chars>". The framers in the
 * UART and RS-485 drivers deliver one LF-terminated printable line per frame. */
static size_t make_line(char *out, size_t size, const char *tag, unsigned seq)
{
    const uint32_t a = esp_random(), b = esp_random(), c = esp_random();
    return (size_t)snprintf(out, size, "%s-%u-%08" PRIX32 "%08" PRIX32 "%08" PRIX32, tag, seq, a, b, c);
}

/* The UART/RS-485 framers deliver the line with its LF (and CR) still attached:
 * compare and print only the text before them. */
static size_t line_len(const proto_uart_frame_t *f)
{
    size_t n = f->length;
    while (n && (f->data[n - 1] == '\n' || f->data[n - 1] == '\r')) { --n; }
    return n;
}

/* ------------------------------------------------------------------ UART1 */

static st_status_t test_uart(char *detail)
{
    printf("  Wiring: UART1 TX GPIO%d -> RX GPIO%d with one jumper wire (nothing else on the UART port).\n",
           PROTO_PIN_UART_TX, PROTO_PIN_UART_RX);
    const proto_uart_config_t cfg = PROTO_UART_CONFIG_DEFAULT();
    esp_err_t err = proto_uart_start(&cfg);
    if (err == ESP_ERR_INVALID_STATE && proto_uart_get_snapshot().active) {
        printf("  UART1 was already started; using its current settings (%d baud).\n",
               proto_uart_get_snapshot().baud_rate);
    } else if (err != ESP_OK) {
        detail_set(detail, "proto_uart_start failed: %s", esp_err_to_name(err));
        return ST_FAIL;
    }
    proto_uart_frame_t frame;
    while (proto_uart_receive(&frame) == ESP_OK) { }   /* drop anything already queued */

    const proto_uart_snapshot_t before = proto_uart_get_snapshot();
    const int rounds = 5;
    int matched = 0, received = 0;
    int64_t worst_us = 0;
    for (int i = 0; i < rounds; ++i) {
        char line[64];
        const size_t len = make_line(line, sizeof(line) - 1, "PSU", (unsigned)i);
        line[len] = '\n';
        const int64_t t0 = now_us();
        err = proto_uart_send((const uint8_t *)line, len + 1);
        if (err != ESP_OK) {
            detail_set(detail, "proto_uart_send failed: %s", esp_err_to_name(err));
            return ST_FAIL;
        }
        bool got = false;
        while (now_us() - t0 < 500000) {
            if (proto_uart_receive(&frame) == ESP_OK) { got = true; break; }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        const int64_t rtt = now_us() - t0;
        line[len] = '\0';
        if (!got) {
            printf("  [%d] sent \"%s\" -> nothing received in 500 ms\n", i, line);
            continue;
        }
        ++received;
        const size_t rlen = line_len(&frame);
        const bool same = rlen == len && memcmp(frame.data, line, len) == 0;
        printf("  [%d] sent \"%s\"\n      recv \"%.*s\"  %s  %" PRId64 " us\n", i, line, (int)rlen,
               (const char *)frame.data, same ? "MATCH" : "MISMATCH", rtt);
        if (same) { ++matched; if (rtt > worst_us) { worst_us = rtt; } }
    }
    const proto_uart_snapshot_t after = proto_uart_get_snapshot();
    const uint32_t tx = after.tx_bytes - before.tx_bytes, rx = after.rx_bytes - before.rx_bytes;
    const uint32_t ferr = after.frame_errors - before.frame_errors;
    printf("  Driver counters: tx_bytes=+%" PRIu32 " rx_bytes=+%" PRIu32 " frame_errors=+%" PRIu32
           " malformed=+%" PRIu32 "\n", tx, rx, ferr, after.malformed - before.malformed);
    if (matched == rounds) {
        detail_set(detail, "%d/%d lines echoed byte-exact at %d baud, tx=%" PRIu32 " rx=%" PRIu32
                   " bytes, worst %" PRId64 " us", matched, rounds, after.baud_rate, tx, rx, worst_us);
        return ST_PASS;
    }
    if (received == 0 && rx == 0) {
        detail_set(detail, "0 bytes back: fit jumper GPIO%d->GPIO%d (tx=%" PRIu32 " bytes sent)",
                   PROTO_PIN_UART_TX, PROTO_PIN_UART_RX, tx);
    } else {
        detail_set(detail, "%d/%d lines matched, rx=%" PRIu32 " bytes, frame_errors=%" PRIu32,
                   matched, rounds, rx, ferr);
    }
    return ST_FAIL;
}

/* ------------------------------------------------------------------ CAN (TWAI) */

static st_status_t test_can(char *detail)
{
    printf("  Wiring: GPIO%d -> SN65HVD230 D (CTX), GPIO%d <- R (CRX), module on 3V3.\n",
           PROTO_PIN_TWAI_TX, PROTO_PIN_TWAI_RX);
    printf("  Mode: TWAI self-test (frames go out through the transceiver and are read back;\n"
           "        no second CAN node is needed). 120 ohm across CANH/CANL is fine to leave on.\n");
    const proto_can_config_t cfg = { .bit_rate = 500000, .mode = PROTO_CAN_MODE_SELF_TEST };
    esp_err_t err = proto_can_start(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        const proto_can_snapshot_t s = proto_can_get_snapshot();
        if (!(s.active && s.mode == PROTO_CAN_MODE_SELF_TEST)) {
            detail_set(detail, "CAN already started in another mode: reboot, then run selftest first");
            return ST_SKIP;
        }
    } else if (err != ESP_OK) {
        detail_set(detail, "proto_can_start failed: %s", esp_err_to_name(err));
        return ST_FAIL;
    }
    proto_can_frame_t rx;
    while (proto_can_receive(&rx) == ESP_OK) { }

    const proto_can_snapshot_t before = proto_can_get_snapshot();
    const int rounds = 5;
    int matched = 0;
    for (int i = 0; i < rounds; ++i) {
        proto_can_frame_t tx;
        memset(&tx, 0, sizeof(tx));
        tx.id = 0x5A0u + (uint32_t)i;
        tx.dlc = 8;
        const uint32_t r0 = esp_random(), r1 = esp_random();
        memcpy(&tx.data[0], &r0, 4);
        memcpy(&tx.data[4], &r1, 4);
        err = proto_can_send(&tx);
        if (err != ESP_OK) {
            printf("  [%d] send id=0x%03" PRIX32 " failed: %s\n", i, tx.id, esp_err_to_name(err));
            continue;
        }
        const int64_t t0 = now_us();
        bool got = false;
        while (now_us() - t0 < 300000) {
            if (proto_can_receive(&rx) == ESP_OK) { got = true; break; }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        print_hex("sent", tx.data, tx.dlc);
        if (!got) { printf("  [%d] id=0x%03" PRIX32 " -> no frame read back in 300 ms\n", i, tx.id); continue; }
        print_hex("recv", rx.data, rx.dlc);
        const bool same = rx.id == tx.id && !rx.extended && rx.dlc == tx.dlc && memcmp(rx.data, tx.data, tx.dlc) == 0;
        printf("  [%d] id sent=0x%03" PRIX32 " recv=0x%03" PRIX32 " dlc=%u  %s\n", i, tx.id, rx.id,
               (unsigned)rx.dlc, same ? "MATCH" : "MISMATCH");
        if (same) { ++matched; }
    }
    const proto_can_snapshot_t s = proto_can_get_snapshot();
    printf("  Controller: state=%d TEC=%" PRIu32 " REC=%" PRIu32 " bus_errors=+%" PRIu32 " tx_failed=+%"
           PRIu32 " rx_frames=+%" PRIu32 "\n", (int)s.state, s.tx_error_counter, s.rx_error_counter,
           s.bus_errors - before.bus_errors, s.tx_failed - before.tx_failed, s.rx_frames - before.rx_frames);
    if (matched == rounds && s.state == PROTO_CAN_STATE_RUNNING && s.bus_errors == before.bus_errors) {
        detail_set(detail, "%d/%d frames read back via transceiver at %" PRIu32 " bit/s, TEC=%" PRIu32
                   " REC=%" PRIu32 ", 0 bus errors", matched, rounds, s.bit_rate, s.tx_error_counter,
                   s.rx_error_counter);
        return ST_PASS;
    }
    detail_set(detail, "%d/%d frames matched, TEC=%" PRIu32 " bus_errors=+%" PRIu32
               " (check CTX/CRX not swapped, transceiver 3V3/GND)", matched, rounds, s.tx_error_counter,
               s.bus_errors - before.bus_errors);
    return ST_FAIL;
}

/* ------------------------------------------------------------------ RS-485 (UART2) */

static bool rs485_wait_line(proto_uart_frame_t *frame, int64_t timeout_us)
{
    const int64_t t0 = now_us();
    while (now_us() - t0 < timeout_us) {
        if (proto_rs485_receive(frame) == ESP_OK) { return true; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

static st_status_t test_rs485(char *detail)
{
    printf("  Wiring: GPIO%d -> module TXD, GPIO%d <- module RXD, module VCC 3V3.\n",
           PROTO_PIN_RS485_TX, PROTO_PIN_RS485_RX);
    printf("  Mode: 9600 8N1, echo kept. Step 1 checks if the module echoes our own line;\n"
           "        step 2 (only if no echo) waits for a line from a second RS-485 node on A/B.\n");
    const proto_rs485_config_t cfg = { .baud_rate = 9600, .discard_echo = false };
    esp_err_t err = proto_rs485_start(&cfg);
    if (err == ESP_ERR_INVALID_STATE && proto_rs485_get_snapshot().active) {
        printf("  RS-485 was already started (discard_echo=%s); results use those settings.\n",
               proto_rs485_get_snapshot().discard_echo ? "on" : "off");
    } else if (err != ESP_OK) {
        detail_set(detail, "proto_rs485_start failed: %s", esp_err_to_name(err));
        return ST_FAIL;
    }
    proto_uart_frame_t frame;
    while (proto_rs485_receive(&frame) == ESP_OK) { }

    const proto_rs485_snapshot_t before = proto_rs485_get_snapshot();
    char line[64];
    const size_t len = make_line(line, sizeof(line) - 1, "PS485", 0);
    line[len] = '\n';
    err = proto_rs485_send((const uint8_t *)line, len + 1);
    line[len] = '\0';
    if (err != ESP_OK) {
        detail_set(detail, "proto_rs485_send failed: %s", esp_err_to_name(err));
        return ST_FAIL;
    }
    printf("  Sent on A/B: \"%s\"\n", line);
    if (rs485_wait_line(&frame, 400000)) {
        const size_t rlen = line_len(&frame);
        const bool same = rlen == len && memcmp(frame.data, line, len) == 0;
        printf("  Read back:   \"%.*s\"  %s\n", (int)rlen, (const char *)frame.data, same ? "MATCH" : "MISMATCH");
        if (same) {
            const proto_rs485_snapshot_t s = proto_rs485_get_snapshot();
            detail_set(detail, "own line echoed through transceiver at %" PRIu32 " baud, tx=%" PRIu32
                       " rx=%" PRIu32 " bytes", s.baud_rate, s.tx_bytes - before.tx_bytes, s.rx_bytes - before.rx_bytes);
            return ST_PASS;
        }
    } else {
        const proto_rs485_snapshot_t s = proto_rs485_get_snapshot();
        printf("  No echo (rx_bytes=+%" PRIu32 ", echo_bytes=+%" PRIu32 "). This module switches its receiver off\n"
               "  while transmitting, so a second node is needed to prove the link.\n",
               s.rx_bytes - before.rx_bytes, s.echo_bytes - before.echo_bytes);
    }
    printf("  Step 2: from a USB-RS485 adapter on A/B (9600 8N1), type any line and press Enter\n"
           "          within 15 s. The adapter's terminal should also show the line above.\n");
    if (rs485_wait_line(&frame, 15000000)) {
        printf("  Received from remote node: \"%.*s\" (%u bytes)\n", (int)line_len(&frame),
               (const char *)frame.data, (unsigned)frame.length);
        const proto_rs485_snapshot_t s = proto_rs485_get_snapshot();
        detail_set(detail, "line from remote node received (%u bytes), frame_errors=%" PRIu32,
                   (unsigned)frame.length, s.frame_errors - before.frame_errors);
        return ST_PASS;
    }
    const proto_rs485_snapshot_t s = proto_rs485_get_snapshot();
    detail_set(detail, "not proven: no echo and no remote line (tx=%" PRIu32 " bytes sent, rx=%" PRIu32 ")",
               s.tx_bytes - before.tx_bytes, s.rx_bytes - before.rx_bytes);
    return ST_FAIL;
}

/* ------------------------------------------------------------------ External I2C (I2C0) */

static st_status_t test_i2c(char *detail)
{
    printf("  Wiring: SDA GPIO%d, SCL GPIO%d, 4.7k pull-ups to 3V3, sensor on 3V3/GND.\n",
           PROTO_PIN_I2C_SDA, PROTO_PIN_I2C_SCL);
    const proto_i2c_config_t cfg = { .frequency_hz = 100000, .internal_pullup = false };
    esp_err_t err = proto_i2c_start(&cfg);
    if (err == ESP_ERR_INVALID_STATE && proto_i2c_get_snapshot().active) {
        printf("  I2C was already started at %" PRIu32 " Hz; using it.\n", proto_i2c_get_snapshot().frequency_hz);
    } else if (err != ESP_OK) {
        detail_set(detail, "proto_i2c_start failed: %s (SDA/SCL held low? pull-ups?)", esp_err_to_name(err));
        return ST_FAIL;
    }
    uint8_t found[16];
    int count = 0, timeouts = 0;
    for (uint8_t a = PROTO_I2C_ADDR_MIN; a <= PROTO_I2C_ADDR_MAX; ++a) {
        err = proto_i2c_probe(a);
        if (err == ESP_OK && count < (int)sizeof(found)) { found[count++] = a; }
        else if (err == ESP_ERR_TIMEOUT) { ++timeouts; }
    }
    printf("  Scan 0x%02X-0x%02X: %d device(s) ACKed", PROTO_I2C_ADDR_MIN, PROTO_I2C_ADDR_MAX, count);
    for (int i = 0; i < count; ++i) { printf(" 0x%02X", found[i]); }
    printf("\n");
    if (timeouts) { printf("  %d probe(s) timed out: a line is stuck low or pull-ups are missing.\n", timeouts); }
    if (count == 0) {
        detail_set(detail, "no device ACKed on GPIO%d/GPIO%d (timeouts=%d): check sensor power and pull-ups",
                   PROTO_PIN_I2C_SDA, PROTO_PIN_I2C_SCL, timeouts);
        return ST_FAIL;
    }
    /* Read a well-known ID register where the address suggests one. */
    char ids[64] = "";
    for (int i = 0; i < count; ++i) {
        uint8_t reg = 0, value = 0;
        const char *what = NULL;
        if (found[i] == 0x68 || found[i] == 0x69) { reg = 0x75; what = "WHO_AM_I(0x75)"; }   /* MPU-6050/9250 */
        else if (found[i] == 0x76 || found[i] == 0x77) { reg = 0xD0; what = "CHIP_ID(0xD0)"; } /* BMP/BME280 */
        if (!what) { continue; }
        proto_i2c_txn_t txn;
        memset(&txn, 0, sizeof(txn));
        txn.address = found[i];
        txn.op = PROTO_I2C_OP_WRITE_READ;
        txn.write_length = 1;
        txn.read_length = 1;
        txn.write_data[0] = reg;
        err = proto_i2c_transact(&txn, &value);
        if (err == ESP_OK) {
            printf("  0x%02X %s = 0x%02X\n", found[i], what, value);
            const size_t used = strlen(ids);
            snprintf(ids + used, sizeof(ids) - used, " 0x%02X:id=0x%02X", found[i], value);
        } else {
            printf("  0x%02X %s read failed: %s\n", found[i], what, esp_err_to_name(err));
        }
    }
    const proto_i2c_snapshot_t s = proto_i2c_get_snapshot();
    char list[64] = "";
    for (int i = 0; i < count; ++i) {
        const size_t used = strlen(list);
        snprintf(list + used, sizeof(list) - used, "%s0x%02X", i ? "," : "", found[i]);
    }
    detail_set(detail, "%d device(s) at %s%s, %" PRIu32 " Hz", count, list, ids, s.frequency_hz);
    return ST_PASS;
}

/* ------------------------------------------------------------------ OLED (I2C1) */

static st_status_t test_oled(char *detail)
{
    printf("  Wiring: OLED SDA GPIO14, SCL GPIO10, VCC 3V3, GND.\n");
    proto_display_info_t d = proto_display_info();
    if (!d.active) {
        const esp_err_t err = proto_ui_runtime_display_start();
        if (err != ESP_OK) {
            detail_set(detail, "SSD1306 did not ACK at 0x3C/0x3D: %s", esp_err_to_name(err));
            return ST_FAIL;
        }
        d = proto_display_info();
    }
    const uint32_t flushes0 = d.flushes, errors0 = d.flush_errors;
    proto_ui_runtime_test_pattern();
    printf("  Test pattern sent to the OLED for 6 s: look at the screen now.\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    const proto_display_info_t e = proto_display_info();
    const uint32_t flushes = e.flushes - flushes0, errors = e.flush_errors - errors0;
    printf("  %s at I2C 0x%02X, %ux%u, frames ACKed in 2 s: %" PRIu32 ", flush errors: %" PRIu32 "\n",
           e.name ? e.name : "?", (unsigned)e.i2c_address, (unsigned)e.width, (unsigned)e.height, flushes, errors);
    if (e.active && flushes > 0 && errors == 0) {
        detail_set(detail, "%s ACKed at 0x%02X, %" PRIu32 " full frames written in 2 s, 0 errors",
                   e.name ? e.name : "OLED", (unsigned)e.i2c_address, flushes);
        return ST_PASS;
    }
    detail_set(detail, "active=%d frames=%" PRIu32 " errors=%" PRIu32, (int)e.active, flushes, errors);
    return ST_FAIL;
}

/* ------------------------------------------------------------------ External SPI (SPI3) */

static bool all_same(const uint8_t *d, size_t n, uint8_t v)
{
    for (size_t i = 0; i < n; ++i) { if (d[i] != v) { return false; } }
    return true;
}

static st_status_t test_spi(char *detail)
{
    printf("  Wiring: loopback jumper MOSI GPIO%d -> MISO GPIO%d (SCLK GPIO%d, CS GPIO%d).\n",
           PROTO_PIN_EXT_SPI_MOSI, PROTO_PIN_EXT_SPI_MISO, PROTO_PIN_EXT_SPI_SCLK, PROTO_PIN_EXT_SPI_CS0);
    const proto_spi_config_t cfg = { .mode = 0, .clock_hz = 1000000 };
    esp_err_t err = proto_spi_start(&cfg);
    if (err == ESP_ERR_INVALID_STATE && proto_spi_get_snapshot().active) {
        printf("  SPI was already started (mode %u, %" PRIu32 " Hz); using it.\n",
               (unsigned)proto_spi_get_snapshot().mode, proto_spi_get_snapshot().clock_hz);
    } else if (err != ESP_OK) {
        detail_set(detail, "proto_spi_start failed: %s", esp_err_to_name(err));
        return ST_FAIL;
    }
    const size_t n = 32;
    uint8_t tx[32], rx[32];
    int good = 0;
    const int rounds = 3;
    for (int r = 0; r < rounds; ++r) {
        esp_fill_random(tx, n);
        memset(rx, 0, n);
        err = proto_spi_transfer(tx, rx, n);
        if (err != ESP_OK) {
            detail_set(detail, "proto_spi_transfer failed: %s", esp_err_to_name(err));
            return ST_FAIL;
        }
        int diff = 0;
        for (size_t i = 0; i < n; ++i) { if (tx[i] != rx[i]) { ++diff; } }
        printf("  [%d] %u bytes, %d differ\n", r, (unsigned)n, diff);
        print_hex("MOSI", tx, 8);
        print_hex("MISO", rx, 8);
        if (diff == 0) { ++good; }
    }
    const proto_spi_snapshot_t s = proto_spi_get_snapshot();
    if (good == rounds) {
        detail_set(detail, "loopback %d x %u random bytes exact, mode %u at %" PRIu32 " Hz", good,
                   (unsigned)n, (unsigned)s.mode, s.clock_hz);
        return ST_PASS;
    }
    /* No loopback: maybe a real SPI device is fitted. Ask for a JEDEC ID. */
    uint8_t jtx[4] = { 0x9F, 0, 0, 0 }, jrx[4] = { 0 };
    err = proto_spi_transfer(jtx, jrx, sizeof(jtx));
    if (err == ESP_OK) {
        printf("  JEDEC 0x9F reply: %02X %02X %02X\n", jrx[1], jrx[2], jrx[3]);
        if (!all_same(&jrx[1], 3, 0xFF) && !all_same(&jrx[1], 3, 0x00)) {
            detail_set(detail, "device on CS0 answered JEDEC ID %02X %02X %02X", jrx[1], jrx[2], jrx[3]);
            return ST_PASS;
        }
    }
    detail_set(detail, "%d/%d loopbacks exact, no JEDEC reply: fit jumper GPIO%d->GPIO%d",
               good, rounds, PROTO_PIN_EXT_SPI_MOSI, PROTO_PIN_EXT_SPI_MISO);
    return ST_FAIL;
}

/* ------------------------------------------------------------------ microSD (SPI2) */

static const char *fat_name(BYTE t)
{
    switch (t) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
#ifdef FS_EXFAT
    case FS_EXFAT: return "exFAT";
#endif
    default: return "unknown";
    }
}

static st_status_t test_sd(char *detail)
{
    printf("  Wiring: SD module VCC 5V (it has an AMS1117), GND, SCK GPIO%d, MOSI GPIO%d, MISO GPIO%d, CS GPIO%d.\n",
           PROTO_PIN_UI_SPI_SCLK, PROTO_PIN_UI_SPI_MOSI, PROTO_PIN_UI_SPI_MISO, PROTO_PIN_SD_CS);
    if (proto_storage_get_snapshot().card_mounted) {
        detail_set(detail, "logger already owns the card ('log start' was used): reboot, then selftest sd");
        return ST_SKIP;
    }
    esp_err_t err = proto_uibus_init();
    if (err != ESP_OK) {
        detail_set(detail, "SPI2 bus init failed: %s", esp_err_to_name(err));
        return ST_FAIL;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = PROTO_UIBUS_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PROTO_PIN_SD_CS;
    slot.host_id = PROTO_UIBUS_HOST;
    const esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,   /* never format the card */
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    const int64_t tm = now_us();
    err = esp_vfs_fat_sdspi_mount(ST_SD_MOUNT, &host, &slot, &mount, &card);
    if (err != ESP_OK) {
        const char *hint = "check 5V power, wiring and that the card is pushed in";
        if (err == ESP_FAIL) { hint = "card answered but has no FAT filesystem: format it FAT16/FAT32 on the PC"; }
        detail_set(detail, "mount failed: %s (%s)", esp_err_to_name(err), hint);
        return ST_FAIL;
    }
    printf("  Mounted in %" PRId64 " ms. Card registers as reported by the card:\n", (now_us() - tm) / 1000);
    sdmmc_card_print_info(stdout, card);
    const uint64_t card_bytes = (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    const char *kind = (card->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC";

    char drv[3] = { (char)('0' + ff_diskio_get_pdrv_card(card)), ':', 0 };
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    const char *fat = "unknown";
    uint64_t free_bytes = 0;
    if (f_getfree(drv, &free_clusters, &fs) == FR_OK && fs) {
        fat = fat_name(fs->fs_type);
        free_bytes = (uint64_t)free_clusters * fs->csize * (uint64_t)card->csd.sector_size;
    }
    printf("  Filesystem: %s, free %" PRIu64 " KB\n", fat, free_bytes / 1024);

    /* Write random data, read it back, compare. */
    static uint8_t wbuf[ST_SD_BYTES], rbuf[ST_SD_BYTES];
    esp_fill_random(wbuf, sizeof(wbuf));
    st_status_t result = ST_FAIL;
    FILE *f = fopen(ST_SD_FILE, "wb");
    if (!f) {
        detail_set(detail, "%s %" PRIu64 " MB %s: cannot create SELFTEST.TXT (card write-protected or full?)",
                   kind, card_bytes / (1024 * 1024), fat);
        goto out;
    }
    int64_t t0 = now_us();
    const size_t wrote = fwrite(wbuf, 1, sizeof(wbuf), f);
    const int wclose = fclose(f);
    const int64_t wus = now_us() - t0;
    f = fopen(ST_SD_FILE, "rb");
    if (!f || wrote != sizeof(wbuf) || wclose != 0) {
        if (f) { fclose(f); }
        detail_set(detail, "write failed: %u of %u bytes written", (unsigned)wrote, (unsigned)sizeof(wbuf));
        goto out;
    }
    memset(rbuf, 0, sizeof(rbuf));
    t0 = now_us();
    const size_t got = fread(rbuf, 1, sizeof(rbuf), f);
    fclose(f);
    const int64_t rus = now_us() - t0;
    size_t first_diff = sizeof(wbuf);
    for (size_t i = 0; i < sizeof(wbuf); ++i) { if (wbuf[i] != rbuf[i]) { first_diff = i; break; } }
    printf("  SELFTEST.TXT: wrote %u bytes in %" PRId64 " us, read %u bytes in %" PRId64 " us\n",
           (unsigned)wrote, wus, (unsigned)got, rus);
    print_hex("written[0..7]", wbuf, 8);
    print_hex("read   [0..7]", rbuf, 8);
    if (got == sizeof(wbuf) && first_diff == sizeof(wbuf)) {
        detail_set(detail, "%s %s, %" PRIu64 " MB, %s; %u random bytes written+read back exact",
                   kind, card->cid.name, card_bytes / (1024 * 1024), fat, (unsigned)sizeof(wbuf));
        printf("  The file stays on the card so it can be checked on a PC.\n");
        result = ST_PASS;
    } else {
        detail_set(detail, "read-back mismatch: read %u bytes, first difference at byte %u",
                   (unsigned)got, (unsigned)first_diff);
    }
out:
    esp_vfs_fat_sdcard_unmount(ST_SD_MOUNT, card);
    printf("  Card unmounted (free for 'log start').\n");
    return result;
}

/* ------------------------------------------------------------------ microSD raw probe */

/* Talks to the card byte by byte at 400 kHz (no SD driver, no filesystem) and prints
 * exactly what comes back on MISO, to tell power / wiring faults apart. */
static esp_err_t raw_xfer(spi_device_handle_t dev, const uint8_t *tx, uint8_t *rx, size_t n)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = n * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_polling_transmit(dev, &t);
}

/* Sends a 6-byte command then 8 clocking bytes; returns the first non-0xFF byte index or -1. */
static int raw_cmd(spi_device_handle_t dev, const char *label, const uint8_t cmd[6], uint8_t rx[14])
{
    uint8_t tx[14];
    memcpy(tx, cmd, 6);
    memset(tx + 6, 0xFF, 8);
    memset(rx, 0, 14);
    gpio_set_level(PROTO_PIN_SD_CS, 0);
    const esp_err_t err = raw_xfer(dev, tx, rx, sizeof(tx));
    gpio_set_level(PROTO_PIN_SD_CS, 1);
    uint8_t ff = 0xFF, dummy;
    (void)raw_xfer(dev, &ff, &dummy, 1);   /* 8 clocks with CS high between commands */
    if (err != ESP_OK) { printf("  %s: SPI error %s\n", label, esp_err_to_name(err)); return -2; }
    printf("  %-5s reply bytes (after the 6 command bytes):", label);
    for (int i = 6; i < 14; ++i) { printf(" %02X", rx[i]); }
    printf("\n");
    for (int i = 6; i < 14; ++i) { if (rx[i] != 0xFF) { return i; } }
    return -1;
}

static st_status_t test_sdraw(char *detail)
{
    printf("  Raw probe on SPI2 at 400 kHz: SCK GPIO%d, MOSI GPIO%d, MISO GPIO%d, CS GPIO%d.\n",
           PROTO_PIN_UI_SPI_SCLK, PROTO_PIN_UI_SPI_MOSI, PROTO_PIN_UI_SPI_MISO, PROTO_PIN_SD_CS);
    if (proto_storage_get_snapshot().card_mounted) {
        detail_set(detail, "logger owns the card: reboot, then selftest sdraw");
        return ST_SKIP;
    }
    esp_err_t err = proto_uibus_init();
    if (err != ESP_OK) { detail_set(detail, "SPI2 bus init failed: %s", esp_err_to_name(err)); return ST_FAIL; }

    const gpio_config_t cs = { .pin_bit_mask = 1ULL << PROTO_PIN_SD_CS, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&cs);
    gpio_set_level(PROTO_PIN_SD_CS, 1);

    /* Idle MISO level with no clock running, read as a GPIO. */
    const int miso_idle = gpio_get_level(PROTO_PIN_UI_SPI_MISO);
    printf("  MISO GPIO%d idle level (CS high): %d\n", PROTO_PIN_UI_SPI_MISO, miso_idle);

    spi_device_interface_config_t dc;
    memset(&dc, 0, sizeof(dc));
    dc.clock_speed_hz = 400000;
    dc.mode = 0;
    dc.spics_io_num = -1;   /* CS driven by hand above */
    dc.queue_size = 1;
    spi_device_handle_t dev = NULL;
    err = spi_bus_add_device(PROTO_UIBUS_HOST, &dc, &dev);
    if (err != ESP_OK) { detail_set(detail, "spi_bus_add_device failed: %s", esp_err_to_name(err)); return ST_FAIL; }

    /* >= 74 clocks with CS high puts the card in SPI mode. */
    uint8_t ff[10], rx0[10];
    memset(ff, 0xFF, sizeof(ff));
    (void)raw_xfer(dev, ff, rx0, sizeof(ff));
    print_hex("80 clocks, CS high, MISO read", rx0, sizeof(rx0));

    static const uint8_t cmd0[6] = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x95 };
    static const uint8_t cmd8[6] = { 0x48, 0x00, 0x00, 0x01, 0xAA, 0x87 };
    uint8_t r[14];
    int i0 = -1;
    uint8_t r1_cmd0 = 0xFF;
    for (int attempt = 0; attempt < 3 && r1_cmd0 != 0x01; ++attempt) {
        i0 = raw_cmd(dev, "CMD0", cmd0, r);
        r1_cmd0 = (i0 >= 0) ? r[i0] : 0xFF;
    }
    const int i8 = raw_cmd(dev, "CMD8", cmd8, r);
    uint8_t r7[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (i8 >= 0) { for (int k = 0; k < 5 && i8 + k < 14; ++k) { r7[k] = r[i8 + k]; } }

    spi_bus_remove_device(dev);
    gpio_set_level(PROTO_PIN_SD_CS, 1);

    bool all_zero = true;
    for (size_t k = 0; k < sizeof(rx0); ++k) { if (rx0[k] != 0x00) { all_zero = false; } }

    if (r1_cmd0 == 0x01 && i8 >= 0 && (r7[0] == 0x01 && r7[3] == 0x01 && r7[4] == 0xAA)) {
        detail_set(detail, "card answers correctly: CMD0 R1=0x01, CMD8 echo 01..01 AA (SD v2 card, wiring OK)");
        return ST_PASS;
    }
    if (r1_cmd0 == 0x01 && i8 >= 0 && r7[0] == 0x05) {
        detail_set(detail, "card answers correctly: CMD0 R1=0x01, CMD8 rejected 0x05 (SD v1 card, wiring OK)");
        return ST_PASS;
    }
    if (all_zero && miso_idle == 0) {
        detail_set(detail, "MISO stuck at 0: card/buffer not driving MISO -> check module VCC=5V, GND, CS GPIO7, MISO GPIO13");
    } else if (i0 < 0) {
        detail_set(detail, "MISO stays 0xFF: card never answers -> check CS GPIO7, MOSI GPIO11, SCK GPIO12, card power");
    } else {
        detail_set(detail, "card answers but wrong bytes (CMD0 R1=0x%02X, CMD8 %02X %02X %02X %02X %02X): "
                   "signal problem, shorten wires / check 5V", r1_cmd0, r7[0], r7[1], r7[2], r7[3], r7[4]);
    }
    return ST_FAIL;
}

/* ------------------------------------------------------------------ Encoder */

static st_status_t test_enc(char *detail)
{
    const proto_encoder_stats_t s0 = proto_encoder_stats();
    if (!s0.started) {
        detail_set(detail, "encoder driver not started (CONFIG_PROTO_UI_AUTOSTART off?)");
        return ST_SKIP;
    }
    int a, b, sw;
    proto_encoder_read_levels(&a, &b, &sw);
    printf("  Pins now: A(GPIO1)=%d B(GPIO2)=%d SW(GPIO6)=%d\n", a, b, sw);
    printf("  Turn the knob a few clicks and press it once within 10 s...\n");
    const int64_t t0 = now_us();
    proto_encoder_stats_t s = s0;
    uint32_t last_d = 0, last_p = 0;
    while (now_us() - t0 < 10000000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s = proto_encoder_stats();
        const uint32_t d = s.detents - s0.detents, p = s.presses - s0.presses;
        if (d != last_d || p != last_p) {
            printf("    detents=%" PRIu32 " presses=%" PRIu32 " edges=%" PRIu32 "\n", d, p, s.isr_edges - s0.isr_edges);
            last_d = d;
            last_p = p;
        }
        if (d >= 3 && p >= 1) { break; }
    }
    const uint32_t d = s.detents - s0.detents, p = s.presses - s0.presses, e = s.isr_edges - s0.isr_edges;
    if (d > 0 && p > 0) {
        detail_set(detail, "%" PRIu32 " detents, %" PRIu32 " presses, %" PRIu32 " A/B edges, %" PRIu32
                   " missed", d, p, e, s.missed_edges - s0.missed_edges);
        return ST_PASS;
    }
    detail_set(detail, "detents=%" PRIu32 " presses=%" PRIu32 " edges=%" PRIu32 "%s", d, p, e,
               e == 0 ? " (no signal on GPIO1/GPIO2)" : "");
    return ST_FAIL;
}

/* ------------------------------------------------------------------ Runner */

static st_test_t s_tests[] = {
    { "uart", test_uart, false, ST_NOT_RUN, "" },
    { "can", test_can, false, ST_NOT_RUN, "" },
    { "rs485", test_rs485, false, ST_NOT_RUN, "" },
    { "i2c", test_i2c, false, ST_NOT_RUN, "" },
    { "oled", test_oled, false, ST_NOT_RUN, "" },
    { "spi", test_spi, false, ST_NOT_RUN, "" },
    { "sd", test_sd, false, ST_NOT_RUN, "" },
    { "sdraw", test_sdraw, true, ST_NOT_RUN, "" },
    { "enc", test_enc, false, ST_NOT_RUN, "" },
};
#define ST_COUNT (sizeof(s_tests) / sizeof(s_tests[0]))

static const char *status_text(st_status_t s)
{
    switch (s) {
    case ST_PASS: return "PASS";
    case ST_FAIL: return "FAIL";
    case ST_SKIP: return "SKIP";
    default: return "-";
    }
}

static SemaphoreHandle_t s_done;
static int s_selected = -1;   /* -1 = all */

static void selftest_task(void *arg)
{
    (void)arg;
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\n================ ProtoScope hardware self-test (TEMPORARY) ================\n");
    printf("Chip ESP32-S3 rev v%d.%d, %d cores | ESP-IDF %s | PSRAM %u KB | uptime %" PRId64 " ms\n",
           chip.revision / 100, chip.revision % 100, chip.cores, esp_get_idf_version(),
           (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024), now_us() / 1000);
    for (size_t i = 0; i < ST_COUNT; ++i) {
        if (s_selected >= 0 && (size_t)s_selected != i) { continue; }
        if (s_selected < 0 && s_tests[i].only_on_request) { continue; }
        st_test_t *t = &s_tests[i];
        printf("\n--- %s ---\n", t->name);
        t->detail[0] = '\0';
        const int64_t t0 = now_us();
        t->status = t->run(t->detail);
        printf("  => %s: %s (%" PRId64 " ms)\n", status_text(t->status), t->detail, (now_us() - t0) / 1000);
    }
    int pass = 0, fail = 0, skip = 0;
    printf("\n=============================== SUMMARY ===================================\n");
    for (size_t i = 0; i < ST_COUNT; ++i) {
        const st_test_t *t = &s_tests[i];
        if (t->status == ST_NOT_RUN) { continue; }
        printf("%-6s %-4s %s\n", t->name, status_text(t->status), t->detail);
        pass += t->status == ST_PASS;
        fail += t->status == ST_FAIL;
        skip += t->status == ST_SKIP;
    }
    printf("Result: %d PASS, %d FAIL, %d SKIP  (uptime %" PRId64 " ms)\n", pass, fail, skip, now_us() / 1000);
    printf("===========================================================================\n");
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

static int selftest_command(int argc, char **argv)
{
    s_selected = -1;
    if (argc == 2 && strcmp(argv[1], "all") != 0) {
        for (size_t i = 0; i < ST_COUNT; ++i) {
            if (strcmp(argv[1], s_tests[i].name) == 0) { s_selected = (int)i; }
        }
        if (s_selected < 0) {
            puts("Usage: selftest [all|uart|can|rs485|i2c|oled|spi|sd|sdraw|enc]");
            return 1;
        }
    } else if (argc > 2) {
        puts("Usage: selftest [all|uart|can|rs485|i2c|oled|spi|sd|sdraw|enc]");
        return 1;
    }
    if (!s_done) { s_done = xSemaphoreCreateBinary(); }
    if (!s_done) { return 1; }
    /* Own task: SD mount and FatFs need more stack than the console REPL task has. */
    if (xTaskCreate(selftest_task, "selftest", 8192, NULL, 5, NULL) != pdPASS) {
        puts("selftest: could not create task (out of memory)");
        return 1;
    }
    xSemaphoreTake(s_done, portMAX_DELAY);
    return 0;
}

esp_err_t proto_selftest_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "selftest",
        .help = "TEMPORARY hardware self-test: selftest [all|uart|can|rs485|i2c|oled|spi|sd|sdraw|enc]",
        .func = selftest_command,
    };
    return esp_console_cmd_register(&cmd);
}
