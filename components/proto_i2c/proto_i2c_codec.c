#include "proto_i2c_codec.h"

bool proto_i2c_address_valid(uint8_t address)
{
    return address >= PROTO_I2C_ADDR_MIN && address <= PROTO_I2C_ADDR_MAX;
}

bool proto_i2c_frequency_supported(uint32_t hz)
{
    return hz == 100000u || hz == 400000u;
}

bool proto_i2c_txn_valid(const proto_i2c_txn_t *txn)
{
    if (!txn || !proto_i2c_address_valid(txn->address)) { return false; }
    if (txn->write_length > PROTO_I2C_MAX_TRANSFER || txn->read_length > PROTO_I2C_MAX_TRANSFER) {
        return false;
    }
    switch (txn->op) {
    case PROTO_I2C_OP_WRITE: return txn->write_length >= 1 && txn->read_length == 0;
    case PROTO_I2C_OP_READ: return txn->write_length == 0 && txn->read_length >= 1;
    case PROTO_I2C_OP_WRITE_READ: return txn->write_length >= 1 && txn->read_length >= 1;
    }
    return false;
}
