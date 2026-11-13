#include "proto_i2c.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "proto_board.h"

#define PROTO_I2C_PORT I2C_NUM_0
#define PROTO_I2C_TIMEOUT_MS 100

static const char *TAG = "i2c";
static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_counter_lock = portMUX_INITIALIZER_UNLOCKED;
static proto_i2c_snapshot_t s_snapshot;

static void add_counter(uint32_t *counter)
{
    portENTER_CRITICAL(&s_counter_lock);
    *counter += 1;
    portEXIT_CRITICAL(&s_counter_lock);
}

static void record_failure(esp_err_t err)
{
    /* The exact error ESP-IDF returns for a NACK differs between versions, so
     * the raw error is kept in last_error for the hardware characterization. */
    portENTER_CRITICAL(&s_counter_lock);
    s_snapshot.last_error = err;
    if (err == ESP_ERR_TIMEOUT) {
        s_snapshot.timeouts++;
    } else if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE) {
        s_snapshot.nacks++;
    } else {
        s_snapshot.bus_errors++;
    }
    portEXIT_CRITICAL(&s_counter_lock);
}

esp_err_t proto_i2c_start(const proto_i2c_config_t *config)
{
    if (!config || !proto_i2c_frequency_supported(config->frequency_hz)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (proto_i2c_get_snapshot().active) { return ESP_ERR_INVALID_STATE; }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) { return ESP_ERR_NO_MEM; }
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = PROTO_I2C_PORT,
        .sda_io_num = PROTO_PIN_I2C_SDA,
        .scl_io_num = PROTO_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = config->internal_pullup,
    };
    const esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }
    portENTER_CRITICAL(&s_counter_lock);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.active = true;
    s_snapshot.frequency_hz = config->frequency_hz;
    s_snapshot.internal_pullup = config->internal_pullup;
    portEXIT_CRITICAL(&s_counter_lock);
    ESP_LOGI(TAG, "I2C controller started: SDA GPIO%d, SCL GPIO%d, %u Hz, internal pull-up %s",
             PROTO_PIN_I2C_SDA, PROTO_PIN_I2C_SCL, (unsigned)config->frequency_hz,
             config->internal_pullup ? "on" : "off");
    return ESP_OK;
}

esp_err_t proto_i2c_probe(uint8_t address)
{
    if (!proto_i2c_address_valid(address)) {
        add_counter(&s_snapshot.rejected);
        return ESP_ERR_INVALID_ARG;
    }
    if (!proto_i2c_get_snapshot().active) { return ESP_ERR_INVALID_STATE; }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        add_counter(&s_snapshot.rejected);
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = i2c_master_probe(s_bus, address, PROTO_I2C_TIMEOUT_MS);
    xSemaphoreGive(s_lock);
    add_counter(&s_snapshot.probes);
    if (err == ESP_OK) {
        add_counter(&s_snapshot.devices_found);
    } else {
        record_failure(err);
    }
    return err;
}

esp_err_t proto_i2c_transact(const proto_i2c_txn_t *txn, uint8_t *read_data)
{
    if (!proto_i2c_txn_valid(txn) || (txn->read_length && !read_data)) {
        add_counter(&s_snapshot.rejected);
        return ESP_ERR_INVALID_ARG;
    }
    const proto_i2c_snapshot_t snapshot = proto_i2c_get_snapshot();
    if (!snapshot.active) { return ESP_ERR_INVALID_STATE; }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        add_counter(&s_snapshot.rejected);
        return ESP_ERR_TIMEOUT;
    }
    add_counter(&s_snapshot.transactions);
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = txn->address,
        .scl_speed_hz = snapshot.frequency_hz,
    };
    i2c_master_dev_handle_t device = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &device_config, &device);
    if (err == ESP_OK) {
        switch (txn->op) {
        case PROTO_I2C_OP_WRITE:
            err = i2c_master_transmit(device, txn->write_data, txn->write_length,
                                      PROTO_I2C_TIMEOUT_MS);
            break;
        case PROTO_I2C_OP_READ:
            err = i2c_master_receive(device, read_data, txn->read_length, PROTO_I2C_TIMEOUT_MS);
            break;
        case PROTO_I2C_OP_WRITE_READ:
            err = i2c_master_transmit_receive(device, txn->write_data, txn->write_length,
                                              read_data, txn->read_length, PROTO_I2C_TIMEOUT_MS);
            break;
        }
        (void)i2c_master_bus_rm_device(device);
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        add_counter(&s_snapshot.completed);
    } else {
        record_failure(err);
    }
    return err;
}

proto_i2c_snapshot_t proto_i2c_get_snapshot(void)
{
    proto_i2c_snapshot_t snapshot;
    portENTER_CRITICAL(&s_counter_lock);
    snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_counter_lock);
    return snapshot;
}
