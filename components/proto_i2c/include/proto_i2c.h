#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_i2c_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ProtoScope is an I2C CONTROLLER only (FR-004/FR-006): it initiates
 * transactions with a configured device. Peripheral mode and passive sniffing
 * are NOT implemented. SDA/SCL are PROTO_PIN_I2C_SDA/SCL (GPIO8/GPIO9). */
typedef struct {
    uint32_t frequency_hz;   /* 100000 or 400000 */
    bool internal_pullup;    /* weak ~45 kohm ESP pull-ups; leave off if the breakout has pull-ups */
} proto_i2c_config_t;

#define PROTO_I2C_CONFIG_DEFAULT() { .frequency_hz = 100000, .internal_pullup = false }

typedef struct {
    bool active;
    uint32_t frequency_hz;
    bool internal_pullup;
    uint32_t probes;          /* addresses probed */
    uint32_t devices_found;   /* probes that were ACKed */
    uint32_t transactions;    /* transfer attempts */
    uint32_t completed;       /* successful transfers */
    uint32_t nacks;           /* device did not acknowledge */
    uint32_t timeouts;
    uint32_t bus_errors;      /* any other driver failure */
    uint32_t rejected;        /* invalid request or busy, never reached the bus */
    esp_err_t last_error;
} proto_i2c_snapshot_t;

/* Start once from the console owner; reboot to change settings. */
esp_err_t proto_i2c_start(const proto_i2c_config_t *config);
/* Address-only probe: ESP_OK = ACK, ESP_ERR_NOT_FOUND = NACK, ESP_ERR_TIMEOUT = bus timeout. */
esp_err_t proto_i2c_probe(uint8_t address);
/* Run one transaction. read_data must hold txn->read_length bytes (may be NULL when 0). */
esp_err_t proto_i2c_transact(const proto_i2c_txn_t *txn, uint8_t *read_data);
proto_i2c_snapshot_t proto_i2c_get_snapshot(void);

#ifdef __cplusplus
}
#endif
