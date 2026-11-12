#include "proto_rs485_timing.h"

bool proto_rs485_baud_supported(uint32_t baud)
{
    return baud == 9600u || baud == 19200u || baud == 38400u || baud == 57600u ||
           baud == 115200u;
}

uint32_t proto_rs485_char_time_us(uint32_t baud)
{
    if (baud == 0) { return 0; }
    return (uint32_t)((10000000ULL + baud - 1u) / baud);
}

uint32_t proto_rs485_guard_us(uint32_t baud)
{
    if (baud == 0) { return 0; }
    return 2u * proto_rs485_char_time_us(baud) + 1000u;
}
