#pragma once

/* Pure-C I2C transaction types and validation (no ESP-IDF dependency). */

#include <stdbool.h>
#include <stdint.h>

#define PROTO_I2C_MAX_TRANSFER 64
#define PROTO_I2C_ADDR_MIN 0x08u /* 0x00-0x07 are reserved */
#define PROTO_I2C_ADDR_MAX 0x77u /* 0x78-0x7F are reserved */

typedef enum {
    PROTO_I2C_OP_WRITE = 0,      /* controller write: write_length >= 1, read_length == 0 */
    PROTO_I2C_OP_READ,           /* controller read: write_length == 0, read_length >= 1 */
    PROTO_I2C_OP_WRITE_READ,     /* write, repeated start, read (register/command read) */
} proto_i2c_op_t;

typedef struct {
    uint8_t address;             /* 7-bit address, not shifted */
    proto_i2c_op_t op;
    uint8_t write_length;
    uint8_t read_length;
    uint8_t write_data[PROTO_I2C_MAX_TRANSFER];
} proto_i2c_txn_t;

bool proto_i2c_address_valid(uint8_t address);
bool proto_i2c_frequency_supported(uint32_t hz); /* 100 kHz or 400 kHz */
bool proto_i2c_txn_valid(const proto_i2c_txn_t *txn);
