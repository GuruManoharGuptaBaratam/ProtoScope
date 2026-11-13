#include "proto_spi_codec.h"

bool proto_spi_mode_valid(uint8_t mode)
{
    return mode <= 3;
}

bool proto_spi_clock_valid(uint32_t clock_hz)
{
    return clock_hz >= PROTO_SPI_CLOCK_MIN_HZ && clock_hz <= PROTO_SPI_CLOCK_MAX_HZ;
}

bool proto_spi_transfer_length_valid(size_t length)
{
    return length >= 1 && length <= PROTO_SPI_MAX_TRANSFER;
}
