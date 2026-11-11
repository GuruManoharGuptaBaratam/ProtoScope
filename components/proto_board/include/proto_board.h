#pragma once

#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Phase-0 pin allocation for the ESP32-S3-DevKitC-1 candidate.
 * GPIO19/20 (native USB), GPIO43/44 (UART0 console), strapping pins,
 * memory-sensitive pins, and possible RGB LED pins are intentionally absent.
 */
typedef enum {
    PROTO_PIN_ENCODER_A = GPIO_NUM_1,
    PROTO_PIN_ENCODER_B = GPIO_NUM_2,
    PROTO_PIN_TWAI_TX = GPIO_NUM_4,
    PROTO_PIN_TWAI_RX = GPIO_NUM_5,
    PROTO_PIN_ENCODER_PUSH = GPIO_NUM_6,
    PROTO_PIN_SD_CS = GPIO_NUM_7,
    PROTO_PIN_I2C_SDA = GPIO_NUM_8,
    PROTO_PIN_I2C_SCL = GPIO_NUM_9,
    PROTO_PIN_TFT_CS = GPIO_NUM_10,
    PROTO_PIN_UI_SPI_MOSI = GPIO_NUM_11,
    PROTO_PIN_UI_SPI_SCLK = GPIO_NUM_12,
    PROTO_PIN_UI_SPI_MISO = GPIO_NUM_13,
    PROTO_PIN_TFT_DC = GPIO_NUM_14,
    PROTO_PIN_RS485_TX = GPIO_NUM_15,
    PROTO_PIN_RS485_RX = GPIO_NUM_16,
    PROTO_PIN_UART_TX = GPIO_NUM_17,
    PROTO_PIN_UART_RX = GPIO_NUM_18,
    PROTO_PIN_TFT_RESET = GPIO_NUM_21,
    PROTO_PIN_EXT_SPI_SCLK = GPIO_NUM_39,
    PROTO_PIN_EXT_SPI_MOSI = GPIO_NUM_40,
    PROTO_PIN_EXT_SPI_MISO = GPIO_NUM_41,
    PROTO_PIN_EXT_SPI_CS0 = GPIO_NUM_42,
    PROTO_PIN_SPARE = GPIO_NUM_47,
} proto_board_pin_t;

/** Validate GPIO range, output capability, and uniqueness. */
esp_err_t proto_board_validate_pin_plan(void);

/**
 * Put allocated Phase-1 pins into non-driving or inactive states.
 * This does not install UART, TWAI, I2C, SPI, TFT, SD, or encoder drivers.
 */
esp_err_t proto_board_init_safe(void);

#ifdef __cplusplus
}
#endif
