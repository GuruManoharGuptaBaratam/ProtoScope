#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_spi_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* External SPI port, controller/host mode only (FR-005/FR-006). Uses SPI3_HOST on
 * GPIO39 (SCLK), GPIO40 (MOSI), GPIO41 (MISO), GPIO42 (CS0). SPI2_HOST is reserved
 * for the later TFT/microSD bus so the two never share a bus (D-004). Passive
 * sniffing and device/peripheral mode are NOT implemented. */
typedef struct {
    uint8_t mode;       /* SPI mode 0..3 */
    uint32_t clock_hz;  /* 1 kHz .. 10 MHz */
} proto_spi_config_t;

#define PROTO_SPI_CONFIG_DEFAULT() { .mode = 0, .clock_hz = 1000000 }

typedef struct {
    bool active;
    uint8_t mode;
    uint32_t clock_hz;
    uint32_t transfers;   /* attempted */
    uint32_t completed;
    uint32_t failures;
    uint32_t rejected;    /* invalid request, never reached the bus */
    uint32_t bytes;       /* bytes clocked in completed transfers */
    esp_err_t last_error;
} proto_spi_snapshot_t;

esp_err_t proto_spi_start(const proto_spi_config_t *config);
/* Full-duplex transfer of 1..64 bytes with CS0 asserted for the whole transfer.
 * rx must hold length bytes. */
esp_err_t proto_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t length);
proto_spi_snapshot_t proto_spi_get_snapshot(void);

#ifdef __cplusplus
}
#endif
