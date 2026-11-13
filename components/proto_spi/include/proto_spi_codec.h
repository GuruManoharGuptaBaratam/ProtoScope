#pragma once

/* Pure-C SPI validation helpers (no ESP-IDF dependency). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_SPI_MAX_TRANSFER 64
#define PROTO_SPI_CLOCK_MIN_HZ 1000u
#define PROTO_SPI_CLOCK_MAX_HZ 10000000u /* conservative for the GPIO-matrix pins */
#define PROTO_SPI_CS_COUNT 1             /* one chip select (CS0) initially */

bool proto_spi_mode_valid(uint8_t mode);          /* 0..3 */
bool proto_spi_clock_valid(uint32_t clock_hz);
bool proto_spi_transfer_length_valid(size_t length); /* 1..64 bytes, full duplex */
