#include "proto_rs485_timing.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(proto_rs485_baud_supported(9600) && proto_rs485_baud_supported(19200));
    assert(proto_rs485_baud_supported(38400) && proto_rs485_baud_supported(57600));
    assert(proto_rs485_baud_supported(115200));
    assert(!proto_rs485_baud_supported(0) && !proto_rs485_baud_supported(4800));
    assert(!proto_rs485_baud_supported(250000));

    assert(proto_rs485_char_time_us(9600) == 1042);   /* 10 bits, rounded up */
    assert(proto_rs485_char_time_us(115200) == 87);
    assert(proto_rs485_char_time_us(0) == 0);

    assert(proto_rs485_guard_us(9600) == 2u * 1042u + 1000u);
    assert(proto_rs485_guard_us(115200) == 2u * 87u + 1000u);
    assert(proto_rs485_guard_us(0) == 0);
    /* A slower line never has a shorter guard time. */
    assert(proto_rs485_guard_us(9600) > proto_rs485_guard_us(19200));
    assert(proto_rs485_guard_us(19200) > proto_rs485_guard_us(115200));
    puts("RS-485 timing: baud support, character time and guard time passed.");
    return 0;
}
