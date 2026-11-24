#include "proto_uibus.h"

#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "proto_board.h"

static const char *TAG = "uibus";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_ready;
static bool s_busy;

bool proto_uibus_ready(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool ready = s_ready;
    portEXIT_CRITICAL(&s_lock);
    return ready;
}

esp_err_t proto_uibus_init(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ready) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    if (s_busy) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;  /* another task is initializing; caller retries */
    }
    s_busy = true;
    portEXIT_CRITICAL(&s_lock);

    const spi_bus_config_t config = {
        .mosi_io_num = PROTO_PIN_UI_SPI_MOSI,
        .miso_io_num = PROTO_PIN_UI_SPI_MISO,
        .sclk_io_num = PROTO_PIN_UI_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PROTO_UIBUS_MAX_TRANSFER,
    };
    const esp_err_t err = spi_bus_initialize(PROTO_UIBUS_HOST, &config, SPI_DMA_CH_AUTO);
    portENTER_CRITICAL(&s_lock);
    s_busy = false;
    s_ready = (err == ESP_OK);
    portEXIT_CRITICAL(&s_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "UI SPI bus ready: SCLK GPIO%d, MOSI GPIO%d, MISO GPIO%d",
                 PROTO_PIN_UI_SPI_SCLK, PROTO_PIN_UI_SPI_MOSI, PROTO_PIN_UI_SPI_MISO);
    }
    return err;
}
