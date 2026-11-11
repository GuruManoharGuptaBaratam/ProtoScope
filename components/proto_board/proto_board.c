#include "proto_board.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board";

static const gpio_num_t s_all_allocated_pins[] = {
    PROTO_PIN_ENCODER_A,      PROTO_PIN_ENCODER_B,
    PROTO_PIN_TWAI_TX,       PROTO_PIN_TWAI_RX,
    PROTO_PIN_ENCODER_PUSH,   PROTO_PIN_SD_CS,
    PROTO_PIN_I2C_SDA,        PROTO_PIN_I2C_SCL,
    PROTO_PIN_TFT_CS,         PROTO_PIN_UI_SPI_MOSI,
    PROTO_PIN_UI_SPI_SCLK,    PROTO_PIN_UI_SPI_MISO,
    PROTO_PIN_TFT_DC,         PROTO_PIN_RS485_TX,
    PROTO_PIN_RS485_RX,       PROTO_PIN_UART_TX,
    PROTO_PIN_UART_RX,        PROTO_PIN_TFT_RESET,
    PROTO_PIN_EXT_SPI_SCLK,   PROTO_PIN_EXT_SPI_MOSI,
    PROTO_PIN_EXT_SPI_MISO,   PROTO_PIN_EXT_SPI_CS0,
    PROTO_PIN_SPARE,
};

static uint64_t mask_for(const gpio_num_t *pins, size_t count)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < count; ++i) {
        mask |= (1ULL << (uint32_t)pins[i]);
    }
    return mask;
}

esp_err_t proto_board_validate_pin_plan(void)
{
    const size_t count = sizeof(s_all_allocated_pins) / sizeof(s_all_allocated_pins[0]);

    for (size_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(s_all_allocated_pins[i]),
                            ESP_ERR_INVALID_ARG, TAG, "Invalid GPIO %d",
                            (int)s_all_allocated_pins[i]);
        for (size_t j = i + 1; j < count; ++j) {
            ESP_RETURN_ON_FALSE(s_all_allocated_pins[i] != s_all_allocated_pins[j],
                                ESP_ERR_INVALID_STATE, TAG,
                                "GPIO %d is assigned more than once",
                                (int)s_all_allocated_pins[i]);
        }
    }

    const gpio_num_t outputs[] = {
        PROTO_PIN_TWAI_TX,       PROTO_PIN_SD_CS,
        PROTO_PIN_I2C_SDA,       PROTO_PIN_I2C_SCL,
        PROTO_PIN_TFT_CS,        PROTO_PIN_UI_SPI_MOSI,
        PROTO_PIN_UI_SPI_SCLK,   PROTO_PIN_TFT_DC,
        PROTO_PIN_RS485_TX,      PROTO_PIN_UART_TX,
        PROTO_PIN_TFT_RESET,     PROTO_PIN_EXT_SPI_SCLK,
        PROTO_PIN_EXT_SPI_MOSI,  PROTO_PIN_EXT_SPI_CS0,
    };
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(outputs[i]),
                            ESP_ERR_INVALID_ARG, TAG, "GPIO %d cannot be an output",
                            (int)outputs[i]);
    }

    return ESP_OK;
}

esp_err_t proto_board_init_safe(void)
{
    ESP_RETURN_ON_ERROR(proto_board_validate_pin_plan(), TAG, "Pin-plan validation failed");

    /* Latch safe levels before enabling output drivers to minimize transitions. */
    const gpio_num_t high_idle_outputs[] = {
        PROTO_PIN_TWAI_TX,
        PROTO_PIN_SD_CS,
        PROTO_PIN_TFT_CS,
        PROTO_PIN_RS485_TX,
        PROTO_PIN_UART_TX,
        PROTO_PIN_TFT_RESET,
        PROTO_PIN_EXT_SPI_CS0,
    };
    const gpio_num_t low_idle_outputs[] = {
        PROTO_PIN_UI_SPI_MOSI,
        PROTO_PIN_UI_SPI_SCLK,
        PROTO_PIN_TFT_DC,
        PROTO_PIN_EXT_SPI_SCLK,
        PROTO_PIN_EXT_SPI_MOSI,
    };

    for (size_t i = 0; i < sizeof(high_idle_outputs) / sizeof(high_idle_outputs[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_set_level(high_idle_outputs[i], 1), TAG,
                            "Failed to latch GPIO %d high", (int)high_idle_outputs[i]);
    }
    for (size_t i = 0; i < sizeof(low_idle_outputs) / sizeof(low_idle_outputs[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_set_level(low_idle_outputs[i], 0), TAG,
                            "Failed to latch GPIO %d low", (int)low_idle_outputs[i]);
    }

    const uint64_t output_mask = mask_for(high_idle_outputs,
                                           sizeof(high_idle_outputs) / sizeof(high_idle_outputs[0])) |
                                 mask_for(low_idle_outputs,
                                           sizeof(low_idle_outputs) / sizeof(low_idle_outputs[0]));
    const gpio_config_t output_config = {
        .pin_bit_mask = output_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "Output GPIO setup failed");

    /* I2C lines are only parked high; the I2C driver is not installed yet. */
    const gpio_num_t i2c_pins[] = {PROTO_PIN_I2C_SDA, PROTO_PIN_I2C_SCL};
    for (size_t i = 0; i < sizeof(i2c_pins) / sizeof(i2c_pins[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_set_level(i2c_pins[i], 1), TAG,
                            "Failed to latch I2C GPIO %d high", (int)i2c_pins[i]);
    }
    const gpio_config_t i2c_config = {
        .pin_bit_mask = mask_for(i2c_pins, sizeof(i2c_pins) / sizeof(i2c_pins[0])),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&i2c_config), TAG, "I2C park-state setup failed");

    const gpio_num_t pulled_up_inputs[] = {
        PROTO_PIN_ENCODER_A,
        PROTO_PIN_ENCODER_B,
        PROTO_PIN_ENCODER_PUSH,
        PROTO_PIN_TWAI_RX,
        PROTO_PIN_RS485_RX,
        PROTO_PIN_UART_RX,
    };
    const gpio_config_t pulled_up_input_config = {
        .pin_bit_mask = mask_for(pulled_up_inputs,
                                 sizeof(pulled_up_inputs) / sizeof(pulled_up_inputs[0])),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pulled_up_input_config), TAG,
                        "Pulled-up input GPIO setup failed");

    const gpio_num_t pulled_down_inputs[] = {
        PROTO_PIN_UI_SPI_MISO,
        PROTO_PIN_EXT_SPI_MISO,
    };
    const gpio_config_t pulled_down_input_config = {
        .pin_bit_mask = mask_for(pulled_down_inputs,
                                 sizeof(pulled_down_inputs) / sizeof(pulled_down_inputs[0])),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pulled_down_input_config), TAG,
                        "Pulled-down input GPIO setup failed");

    ESP_LOGI(TAG, "Pin plan validated; allocated GPIOs parked in Phase-1 safe states");
    return ESP_OK;
}
