#include "proto_rs485.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "proto_board.h"

#define PROTO_RS485_PORT UART_NUM_2
#define PROTO_RS485_TX_TIMEOUT_US 500000LL
#define PROTO_RS485_HOLD_FOREVER INT64_MAX

static const char *TAG = "rs485";
static QueueHandle_t s_events;
static QueueHandle_t s_frames;
static SemaphoreHandle_t s_tx_lock;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static proto_rs485_snapshot_t s_snapshot;
static int64_t s_hold_until_us; /* guarded by s_lock; RX before this is echo */
static bool s_discard_echo;     /* written once before the RX task starts */

static void add_counter(uint32_t *counter, uint32_t amount)
{
    portENTER_CRITICAL(&s_lock);
    *counter += amount;
    portEXIT_CRITICAL(&s_lock);
}

static void set_hold(int64_t until_us)
{
    portENTER_CRITICAL(&s_lock);
    s_hold_until_us = until_us;
    portEXIT_CRITICAL(&s_lock);
}

static bool hold_active(int64_t now_us)
{
    if (!s_discard_echo) { return false; }
    portENTER_CRITICAL(&s_lock);
    const bool active = now_us < s_hold_until_us;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

static void expire_partial(proto_uart_framer_t *framer)
{
    if (proto_uart_framer_expire(framer, (uint64_t)esp_timer_get_time())) {
        add_counter(&s_snapshot.partial_timeouts, 1);
    }
}

static void consume_bytes(proto_uart_framer_t *framer, size_t remaining)
{
    uint8_t bytes[128];
    proto_uart_frame_t frame;
    expire_partial(framer);
    while (remaining) {
        const size_t wanted = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
        const int count = uart_read_bytes(PROTO_RS485_PORT, bytes, wanted, 0);
        if (count <= 0) {
            break;
        }
        remaining -= (size_t)count;
        if (hold_active(esp_timer_get_time())) {
            /* Own echo or a collision while we transmit: never frame it. */
            add_counter(&s_snapshot.echo_bytes, (uint32_t)count);
            memset(framer, 0, sizeof(*framer));
            continue;
        }
        add_counter(&s_snapshot.rx_bytes, (uint32_t)count);
        for (int i = 0; i < count; ++i) {
            const proto_uart_frame_result_t result = proto_uart_framer_feed(
                framer, bytes[i], (uint64_t)esp_timer_get_time(), &frame);
            switch (result) {
            case PROTO_UART_FRAME_READY:
                add_counter(&s_snapshot.rx_frames, 1);
                if (xQueueSend(s_frames, &frame, 0) != pdTRUE) {
                    add_counter(&s_snapshot.rx_queue_drops, 1);
                }
                break;
            case PROTO_UART_FRAME_MALFORMED:
                add_counter(&s_snapshot.malformed, 1);
                break;
            case PROTO_UART_FRAME_TOO_LONG:
                add_counter(&s_snapshot.overlong, 1);
                break;
            case PROTO_UART_FRAME_PENDING:
                break;
            }
        }
    }
}

static void resync_unless_transmitting(proto_uart_framer_t *framer)
{
    const uint64_t now = (uint64_t)esp_timer_get_time();
    if (hold_active((int64_t)now)) {
        memset(framer, 0, sizeof(*framer));
    } else {
        proto_uart_framer_resync(framer, now);
    }
}

static void rx_task(void *context)
{
    (void)context;
    proto_uart_framer_t framer = {0};
    uart_event_t event;
    for (;;) {
        if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
            switch (event.type) {
            case UART_DATA:
                consume_bytes(&framer, event.size);
                break;
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                add_counter(event.type == UART_FIFO_OVF ? &s_snapshot.fifo_overflows :
                            &s_snapshot.buffer_full, 1);
                uart_flush_input(PROTO_RS485_PORT);
                xQueueReset(s_events);
                resync_unless_transmitting(&framer);
                break;
            case UART_FRAME_ERR:
            case UART_PARITY_ERR:
            case UART_BREAK:
                if (event.type == UART_FRAME_ERR) {
                    add_counter(&s_snapshot.frame_errors, 1);
                } else if (event.type == UART_PARITY_ERR) {
                    add_counter(&s_snapshot.parity_errors, 1);
                } else {
                    add_counter(&s_snapshot.breaks, 1);
                }
                resync_unless_transmitting(&framer);
                break;
            default:
                break;
            }
        } else {
            size_t available = 0;
            if (uart_get_buffered_data_len(PROTO_RS485_PORT, &available) == ESP_OK && available) {
                consume_bytes(&framer, available);
            } else {
                expire_partial(&framer);
            }
        }
    }
}

esp_err_t proto_rs485_start(const proto_rs485_config_t *config)
{
    if (!config || !proto_rs485_baud_supported(config->baud_rate)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (proto_rs485_get_snapshot().active || uart_is_driver_installed(PROTO_RS485_PORT)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_frames = xQueueCreate(16, sizeof(proto_uart_frame_t));
    s_tx_lock = xSemaphoreCreateMutex();
    if (!s_frames || !s_tx_lock) {
        if (s_frames) { vQueueDelete(s_frames); }
        if (s_tx_lock) { vSemaphoreDelete(s_tx_lock); }
        s_frames = NULL;
        s_tx_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_discard_echo = config->discard_echo;
    set_hold(0);

    bool installed = false;
    esp_err_t err = uart_driver_install(PROTO_RS485_PORT, 2048, 0, 32, &s_events, 0);
    if (err != ESP_OK) { goto fail; }
    installed = true;
    const uart_config_t uart_config = {
        .baud_rate = (int)config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        /* Independent of the foundation's dynamic CPU/APB frequency policy. */
        .source_clk = UART_SCLK_XTAL,
    };
    err = uart_param_config(PROTO_RS485_PORT, &uart_config);
    if (err != ESP_OK) { goto fail; }
    /* Plain UART only: the module controls direction itself, so RTS/DE is unused
     * and no UART_MODE_RS485_* mode is selected. */
    err = uart_set_pin(PROTO_RS485_PORT, PROTO_PIN_RS485_TX, PROTO_PIN_RS485_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { goto fail; }
    err = uart_set_rx_timeout(PROTO_RS485_PORT, 2);
    if (err != ESP_OK) { goto fail; }
    if (xTaskCreate(rx_task, "proto_rs485_rx", 4096, NULL, 6, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    portENTER_CRITICAL(&s_lock);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.active = true;
    s_snapshot.baud_rate = config->baud_rate;
    s_snapshot.discard_echo = config->discard_echo;
    s_snapshot.guard_us = proto_rs485_guard_us(config->baud_rate);
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "RS-485 (UART2) started: TX GPIO%d, RX GPIO%d, %u baud 8N1, echo discard %s",
             PROTO_PIN_RS485_TX, PROTO_PIN_RS485_RX, (unsigned)config->baud_rate,
             config->discard_echo ? "on" : "off");
    return ESP_OK;

fail:
    if (installed) { uart_driver_delete(PROTO_RS485_PORT); }
    vQueueDelete(s_frames);
    vSemaphoreDelete(s_tx_lock);
    s_frames = NULL;
    s_tx_lock = NULL;
    s_events = NULL;
    return err;
}

esp_err_t proto_rs485_send(const uint8_t *data, size_t length)
{
    if (!data || length == 0 || length > PROTO_UART_FRAME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    const proto_rs485_snapshot_t snapshot = proto_rs485_get_snapshot();
    if (!snapshot.active) { return ESP_ERR_INVALID_STATE; }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        add_counter(&s_snapshot.tx_failures, 1);
        return ESP_ERR_TIMEOUT;
    }
    /* Everything received from now until the guard time after the last bit is
     * our own echo (or a collision) and must not become a "received" frame. */
    set_hold(PROTO_RS485_HOLD_FOREVER);
    const int64_t deadline = esp_timer_get_time() + PROTO_RS485_TX_TIMEOUT_US;
    size_t sent = 0;
    esp_err_t err = ESP_OK;
    while (sent < length && esp_timer_get_time() < deadline) {
        const int count = uart_tx_chars(PROTO_RS485_PORT, (const char *)data + sent,
                                        (uint32_t)(length - sent));
        if (count < 0) { err = ESP_FAIL; break; }
        sent += (size_t)count;
        if (sent < length) { vTaskDelay(1); }
    }
    const int64_t remaining_us = deadline - esp_timer_get_time();
    if (err == ESP_OK) {
        err = (sent != length || remaining_us <= 0) ? ESP_ERR_TIMEOUT :
              uart_wait_tx_done(PROTO_RS485_PORT, pdMS_TO_TICKS((remaining_us + 999) / 1000));
    }
    set_hold(esp_timer_get_time() + (int64_t)snapshot.guard_us);
    add_counter(&s_snapshot.tx_bytes, (uint32_t)sent);
    add_counter(err == ESP_OK ? &s_snapshot.tx_completed : &s_snapshot.tx_failures, 1);
    xSemaphoreGive(s_tx_lock);
    return err;
}

esp_err_t proto_rs485_receive(proto_uart_frame_t *frame)
{
    if (!frame) { return ESP_ERR_INVALID_ARG; }
    if (!proto_rs485_get_snapshot().active) { return ESP_ERR_INVALID_STATE; }
    return xQueueReceive(s_frames, frame, 0) == pdTRUE ? ESP_OK : ESP_ERR_NOT_FOUND;
}

proto_rs485_snapshot_t proto_rs485_get_snapshot(void)
{
    proto_rs485_snapshot_t snapshot;
    portENTER_CRITICAL(&s_lock);
    snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
    return snapshot;
}
