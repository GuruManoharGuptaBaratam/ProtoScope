#pragma once

/* Pure-C timing helpers (no ESP-IDF dependency) so they can be host-tested. */

#include <stdbool.h>
#include <stdint.h>

/* Phase 4 supports 8N1 at these rates; characterize each against the real module. */
bool proto_rs485_baud_supported(uint32_t baud);

/* Duration of one 8N1 character (10 bit times), rounded up. 0 if baud is 0. */
uint32_t proto_rs485_char_time_us(uint32_t baud);

/* Time after the last transmitted bit during which received bytes are treated
 * as the module's echo / own-transmit turnaround: 2 character times + 1 ms
 * allowance for RX interrupt and task latency. 0 if baud is 0. */
uint32_t proto_rs485_guard_us(uint32_t baud);
