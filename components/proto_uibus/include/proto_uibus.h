#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "hal/spi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The UI SPI bus (SPI2_HOST) used by the microSD card (D-004): SCLK GPIO12, MOSI GPIO11,
 * MISO GPIO13, SD chip select GPIO7. The external routed SPI port uses SPI3_HOST and
 * different pins. The 0.96 inch OLED is I2C, so the bus carries only the SD card; the
 * bus layer still allows a second SPI device later without sharing the external port. */
#define PROTO_UIBUS_HOST SPI2_HOST
#define PROTO_UIBUS_MAX_TRANSFER 4096

/* Idempotent and thread-safe: initializes the bus on first use. */
esp_err_t proto_uibus_init(void);
bool proto_uibus_ready(void);

#ifdef __cplusplus
}
#endif
