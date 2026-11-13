#include "proto_driver.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "proto_can.h"
#include "proto_i2c.h"
#include "proto_rs485.h"
#include "proto_spi.h"
#include "proto_uart.h"

static portMUX_TYPE s_id_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_next_id = 1;

uint64_t proto_event_next_id(void)
{
    portENTER_CRITICAL(&s_id_lock);
    const uint64_t id = s_next_id++;
    portEXIT_CRITICAL(&s_id_lock);
    return id;
}

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static proto_event_status_t status_for(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return PROTO_EVENT_STATUS_OK;
    case ESP_ERR_TIMEOUT: return PROTO_EVENT_STATUS_TIMEOUT;
    case ESP_ERR_NOT_FOUND:
    case ESP_ERR_INVALID_RESPONSE: return PROTO_EVENT_STATUS_NACK;
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_STATE:
    case ESP_ERR_NOT_SUPPORTED: return PROTO_EVENT_STATUS_REJECTED;
    default: return PROTO_EVENT_STATUS_BUS_ERROR;
    }
}

/* TX completion event for UART / RS-485 / CAN: the request echoed with status. */
static void fill_tx_result(proto_event_t *result, const proto_event_t *request, esp_err_t err)
{
    if (!result) { return; }
    *result = *request;
    result->event_id = proto_event_next_id();
    result->correlation_id = request->event_id;
    result->direction = PROTO_DIR_TX;
    result->status = status_for(err);
    result->error_code = (int32_t)err;
    result->timestamp_us = now_us();
}

static esp_err_t check_request(const proto_event_t *request, proto_protocol_t protocol)
{
    if (!request || request->protocol != protocol || request->direction != PROTO_DIR_TX ||
        proto_event_validate(request) != PROTO_EVENT_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/* ---- UART ------------------------------------------------------------------ */
static bool uart_active(void) { return proto_uart_get_snapshot().active; }

static esp_err_t uart_receive(proto_event_t *event)
{
    proto_uart_frame_t frame;
    const esp_err_t err = proto_uart_receive(&frame);
    if (err != ESP_OK) { return err; }
    if (!proto_event_from_serial(event, PROTO_PROTOCOL_UART, &frame,
                                 (uint32_t)proto_uart_get_snapshot().baud_rate, PROTO_DIR_RX)) {
        return ESP_FAIL;
    }
    event->event_id = proto_event_next_id();
    return ESP_OK;
}

static esp_err_t uart_submit(const proto_event_t *request, proto_event_t *result)
{
    esp_err_t err = check_request(request, PROTO_PROTOCOL_UART);
    if (err == ESP_OK) {
        uint8_t bytes[PROTO_UART_FRAME_MAX];
        size_t length = 0;
        err = proto_event_to_serial(request, bytes, sizeof(bytes), &length) ?
              proto_uart_send(bytes, length) : ESP_ERR_INVALID_ARG;
    }
    fill_tx_result(result, request, err);
    return err;
}

/* ---- RS-485 ---------------------------------------------------------------- */
static bool rs485_active(void) { return proto_rs485_get_snapshot().active; }

static esp_err_t rs485_receive(proto_event_t *event)
{
    proto_uart_frame_t frame;
    const esp_err_t err = proto_rs485_receive(&frame);
    if (err != ESP_OK) { return err; }
    if (!proto_event_from_serial(event, PROTO_PROTOCOL_RS485, &frame,
                                 proto_rs485_get_snapshot().baud_rate, PROTO_DIR_RX)) {
        return ESP_FAIL;
    }
    event->event_id = proto_event_next_id();
    return ESP_OK;
}

static esp_err_t rs485_submit(const proto_event_t *request, proto_event_t *result)
{
    esp_err_t err = check_request(request, PROTO_PROTOCOL_RS485);
    if (err == ESP_OK) {
        uint8_t bytes[PROTO_UART_FRAME_MAX];
        size_t length = 0;
        err = proto_event_to_serial(request, bytes, sizeof(bytes), &length) ?
              proto_rs485_send(bytes, length) : ESP_ERR_INVALID_ARG;
    }
    fill_tx_result(result, request, err);
    return err;
}

/* ---- CAN ------------------------------------------------------------------- */
static bool can_active(void) { return proto_can_get_snapshot().active; }

static esp_err_t can_receive(proto_event_t *event)
{
    proto_can_frame_t frame;
    const esp_err_t err = proto_can_receive(&frame);
    if (err != ESP_OK) { return err; }
    if (!proto_event_from_can(event, &frame, PROTO_DIR_RX)) { return ESP_FAIL; }
    event->event_id = proto_event_next_id();
    return ESP_OK;
}

static esp_err_t can_submit(const proto_event_t *request, proto_event_t *result)
{
    esp_err_t err = check_request(request, PROTO_PROTOCOL_CAN);
    if (err == ESP_OK) {
        proto_can_frame_t frame;
        err = proto_event_to_can(request, &frame) ? proto_can_send(&frame) : ESP_ERR_INVALID_ARG;
    }
    fill_tx_result(result, request, err);
    return err;
}

/* ---- I2C (controller transaction) ------------------------------------------- */
static bool i2c_active(void) { return proto_i2c_get_snapshot().active; }

static esp_err_t i2c_submit(const proto_event_t *request, proto_event_t *result)
{
    esp_err_t err = check_request(request, PROTO_PROTOCOL_I2C);
    uint8_t read_data[PROTO_I2C_MAX_TRANSFER] = {0};
    if (err == ESP_OK) {
        proto_i2c_txn_t txn;
        err = proto_event_to_i2c(request, &txn) ? proto_i2c_transact(&txn, read_data) :
              ESP_ERR_INVALID_ARG;
    }
    if (result) {
        if (request && request->protocol == PROTO_PROTOCOL_I2C &&
            proto_event_i2c_result(result, request, read_data, status_for(err), (int32_t)err,
                                   now_us())) {
            result->event_id = proto_event_next_id();
        } else {
            memset(result, 0, sizeof(*result));
        }
    }
    return err;
}

/* ---- SPI (controller transaction) -------------------------------------------- */
static bool spi_active(void) { return proto_spi_get_snapshot().active; }

static esp_err_t spi_submit(const proto_event_t *request, proto_event_t *result)
{
    esp_err_t err = check_request(request, PROTO_PROTOCOL_SPI);
    uint8_t rx[PROTO_SPI_MAX_TRANSFER] = {0};
    if (err == ESP_OK) {
        uint8_t tx[PROTO_SPI_MAX_TRANSFER];
        size_t length = 0;
        err = proto_event_to_spi(request, tx, sizeof(tx), &length) ?
              proto_spi_transfer(tx, rx, length) : ESP_ERR_INVALID_ARG;
    }
    if (result) {
        const proto_spi_snapshot_t spi = proto_spi_get_snapshot();
        if (request && request->protocol == PROTO_PROTOCOL_SPI &&
            proto_event_spi_result(result, request, rx, spi.mode, spi.clock_hz, status_for(err),
                                   (int32_t)err, now_us())) {
            result->event_id = proto_event_next_id();
        } else {
            memset(result, 0, sizeof(*result));
        }
    }
    return err;
}

static const proto_driver_t k_drivers[PROTO_PROTOCOL_COUNT] = {
    [PROTO_PROTOCOL_UART] = { PROTO_PROTOCOL_UART, "UART",
        PROTO_CAP_RX_EVENT | PROTO_CAP_TX | PROTO_CAP_BYTE_STREAM, PROTO_UART_FRAME_MAX,
        uart_active, uart_receive, uart_submit },
    [PROTO_PROTOCOL_RS485] = { PROTO_PROTOCOL_RS485, "RS485",
        PROTO_CAP_RX_EVENT | PROTO_CAP_TX | PROTO_CAP_BYTE_STREAM | PROTO_CAP_HALF_DUPLEX,
        PROTO_UART_FRAME_MAX, rs485_active, rs485_receive, rs485_submit },
    [PROTO_PROTOCOL_CAN] = { PROTO_PROTOCOL_CAN, "CAN",
        PROTO_CAP_RX_EVENT | PROTO_CAP_TX | PROTO_CAP_FRAME_ORIENTED, PROTO_CAN_MAX_DLC,
        can_active, can_receive, can_submit },
    [PROTO_PROTOCOL_I2C] = { PROTO_PROTOCOL_I2C, "I2C",
        PROTO_CAP_TX | PROTO_CAP_CONTROLLER_TRANSACTION | PROTO_CAP_CONTROLLER_ROLE,
        PROTO_I2C_MAX_TRANSFER, i2c_active, NULL, i2c_submit },
    [PROTO_PROTOCOL_SPI] = { PROTO_PROTOCOL_SPI, "SPI",
        PROTO_CAP_TX | PROTO_CAP_CONTROLLER_TRANSACTION | PROTO_CAP_CONTROLLER_ROLE |
        PROTO_CAP_FULL_DUPLEX_TRANSFER, PROTO_SPI_MAX_TRANSFER, spi_active, NULL, spi_submit },
};

const proto_driver_t *proto_driver_get(proto_protocol_t protocol)
{
    return (unsigned)protocol < PROTO_PROTOCOL_COUNT ? &k_drivers[protocol] : NULL;
}
