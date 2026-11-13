#include "proto_i2c_codec.h"
#include "proto_spi_codec.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(!proto_i2c_address_valid(0x00) && !proto_i2c_address_valid(0x07));
    assert(proto_i2c_address_valid(0x08) && proto_i2c_address_valid(0x48) &&
           proto_i2c_address_valid(0x77));
    assert(!proto_i2c_address_valid(0x78) && !proto_i2c_address_valid(0x7F) &&
           !proto_i2c_address_valid(0xFF));
    assert(proto_i2c_frequency_supported(100000) && proto_i2c_frequency_supported(400000));
    assert(!proto_i2c_frequency_supported(0) && !proto_i2c_frequency_supported(1000000));

    proto_i2c_txn_t t;
    memset(&t, 0, sizeof(t));
    t.address = 0x68; t.op = PROTO_I2C_OP_WRITE; t.write_length = 2;
    assert(proto_i2c_txn_valid(&t));
    t.read_length = 1;
    assert(!proto_i2c_txn_valid(&t));                 /* write must not read */
    t.op = PROTO_I2C_OP_WRITE_READ;
    assert(proto_i2c_txn_valid(&t));
    t.write_length = 0;
    assert(!proto_i2c_txn_valid(&t));                 /* write-read needs a write */
    t.op = PROTO_I2C_OP_READ;
    assert(proto_i2c_txn_valid(&t));                  /* read with no write bytes */
    t.write_length = 1;
    assert(!proto_i2c_txn_valid(&t));                 /* read must not carry write bytes */
    t.write_length = 0;
    t.read_length = 0;
    assert(!proto_i2c_txn_valid(&t));
    t.read_length = 65;
    assert(!proto_i2c_txn_valid(&t));
    t.read_length = 64; t.address = 0x03;
    assert(!proto_i2c_txn_valid(&t));
    assert(!proto_i2c_txn_valid(NULL));

    assert(proto_spi_mode_valid(0) && proto_spi_mode_valid(3) && !proto_spi_mode_valid(4));
    assert(!proto_spi_clock_valid(999) && proto_spi_clock_valid(1000) &&
           proto_spi_clock_valid(10000000) && !proto_spi_clock_valid(10000001));
    assert(!proto_spi_transfer_length_valid(0) && proto_spi_transfer_length_valid(1) &&
           proto_spi_transfer_length_valid(64) && !proto_spi_transfer_length_valid(65));
    puts("I2C/SPI codec: addresses, frequencies, transaction shapes, modes and clocks passed.");
    return 0;
}
