#include "proto_spi.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "proto_board.h"

#define PROTO_SPI_HOST SPI3_HOST

static const char *TAG = "spi";
static spi_device_handle_t s_device;
static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_counter_lock = portMUX_INITIALIZER_UNLOCKED;
static proto_spi_snapshot_t s_snapshot;

static void add_counter(uint32_t *counter)
{
    portENTER_CRITICAL(&s_counter_lock);
    *counter += 1;
    portEXIT_CRITICAL(&s_counter_lock);
}

esp_err_t proto_spi_start(const proto_spi_config_t *config)
{
    if (!config || !proto_spi_mode_valid(config->mode) || !proto_spi_clock_valid(config->clock_hz)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (proto_spi_get_snapshot().active) { return ESP_ERR_INVALID_STATE; }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) { return ESP_ERR_NO_MEM; }
    const spi_bus_config_t bus_config = {
        .mosi_io_num = PROTO_PIN_EXT_SPI_MOSI,
        .miso_io_num = PROTO_PIN_EXT_SPI_MISO,
        .sclk_io_num = PROTO_PIN_EXT_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PROTO_SPI_MAX_TRANSFER,
    };
    /* No DMA: transfers are at most 64 bytes and use the hardware FIFO. */
    esp_err_t err = spi_bus_initialize(PROTO_SPI_HOST, &bus_config, SPI_DMA_DISABLED);
    if (err != ESP_OK) { goto fail_lock; }
    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = (int)config->clock_hz,
        .mode = config->mode,
        .spics_io_num = PROTO_PIN_EXT_SPI_CS0,
        .queue_size = 1,
    };
    err = spi_bus_add_device(PROTO_SPI_HOST, &device_config, &s_device);
    if (err != ESP_OK) {
        (void)spi_bus_free(PROTO_SPI_HOST);
        goto fail_lock;
    }
    portENTER_CRITICAL(&s_counter_lock);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.active = true;
    s_snapshot.mode = config->mode;
    s_snapshot.clock_hz = config->clock_hz;
    portEXIT_CRITICAL(&s_counter_lock);
    ESP_LOGI(TAG, "SPI controller started: SCLK GPIO%d, MOSI GPIO%d, MISO GPIO%d, CS0 GPIO%d, "
             "mode %u, %u Hz", PROTO_PIN_EXT_SPI_SCLK, PROTO_PIN_EXT_SPI_MOSI,
             PROTO_PIN_EXT_SPI_MISO, PROTO_PIN_EXT_SPI_CS0, (unsigned)config->mode,
             (unsigned)config->clock_hz);
    return ESP_OK;

fail_lock:
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    return err;
}

esp_err_t proto_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t length)
{
    if (!tx || !rx || !proto_spi_transfer_length_valid(length)) {
        add_counter(&s_snapshot.rejected);
        return ESP_ERR_INVALID_ARG;
    }
    if (!proto_spi_get_snapshot().active) { return ESP_ERR_INVALID_STATE; }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        add_counter(&s_snapshot.rejected);
        return ESP_ERR_TIMEOUT;
    }
    uint8_t tx_local[PROTO_SPI_MAX_TRANSFER] __attribute__((aligned(4)));
    uint8_t rx_local[PROTO_SPI_MAX_TRANSFER] __attribute__((aligned(4)));
    memcpy(tx_local, tx, length);
    memset(rx_local, 0, sizeof(rx_local));
    spi_transaction_t transaction;
    memset(&transaction, 0, sizeof(transaction));
    transaction.length = length * 8;
    transaction.tx_buffer = tx_local;
    transaction.rx_buffer = rx_local;
    add_counter(&s_snapshot.transfers);
    const esp_err_t err = spi_device_polling_transmit(s_device, &transaction);
    xSemaphoreGive(s_lock);
    portENTER_CRITICAL(&s_counter_lock);
    if (err == ESP_OK) {
        s_snapshot.completed++;
        s_snapshot.bytes += (uint32_t)length;
    } else {
        s_snapshot.failures++;
        s_snapshot.last_error = err;
    }
    portEXIT_CRITICAL(&s_counter_lock);
    if (err == ESP_OK) { memcpy(rx, rx_local, length); }
    return err;
}

proto_spi_snapshot_t proto_spi_get_snapshot(void)
{
    proto_spi_snapshot_t snapshot;
    portENTER_CRITICAL(&s_counter_lock);
    snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_counter_lock);
    return snapshot;
}
