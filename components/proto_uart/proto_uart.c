#include "proto_uart.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "proto_board.h"

#define PROTO_UART_PORT UART_NUM_1
#define PROTO_UART_TX_TIMEOUT_US 500000LL

static const char *TAG = "uart";
static QueueHandle_t s_events;
static QueueHandle_t s_frames;
static SemaphoreHandle_t s_tx_lock;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static proto_uart_snapshot_t s_snapshot;

static void add_counter(uint32_t *counter, uint32_t amount)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    *counter += amount;
    portEXIT_CRITICAL(&s_snapshot_lock);
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
        const int count = uart_read_bytes(PROTO_UART_PORT, bytes, wanted, 0);
        if (count <= 0) {
            break;
        }
        remaining -= (size_t)count;
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
                uart_flush_input(PROTO_UART_PORT);
                xQueueReset(s_events);
                proto_uart_framer_resync(&framer, (uint64_t)esp_timer_get_time());
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
                proto_uart_framer_resync(&framer, (uint64_t)esp_timer_get_time());
                break;
            default:
                break;
            }
        } else {
            /* Drain buffered bytes even if an ISR data notification was lost.
             * ESP-IDF has no public counter for dropped event notifications. */
            size_t available = 0;
            if (uart_get_buffered_data_len(PROTO_UART_PORT, &available) == ESP_OK && available) {
                consume_bytes(&framer, available);
            } else {
                expire_partial(&framer);
            }
        }
    }
}

esp_err_t proto_uart_start(const proto_uart_config_t *config)
{
    if (!config || (config->baud_rate != 9600 && config->baud_rate != 115200) ||
        config->data_bits != UART_DATA_8_BITS || config->parity != UART_PARITY_DISABLE ||
        config->stop_bits != UART_STOP_BITS_1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (proto_uart_get_snapshot().active || uart_is_driver_installed(PROTO_UART_PORT)) {
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

    bool installed = false;
    esp_err_t err = uart_driver_install(PROTO_UART_PORT, 2048, 0, 32, &s_events, 0);
    if (err != ESP_OK) { goto fail; }
    installed = true;
    const uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = config->data_bits,
        .parity = config->parity,
        .stop_bits = config->stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        /* Independent of the foundation's dynamic CPU/APB frequency policy. */
        .source_clk = UART_SCLK_XTAL,
    };
    err = uart_param_config(PROTO_UART_PORT, &uart_config);
    if (err != ESP_OK) { goto fail; }
    err = uart_set_pin(PROTO_UART_PORT, PROTO_PIN_UART_TX, PROTO_PIN_UART_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { goto fail; }
    err = uart_set_rx_timeout(PROTO_UART_PORT, 2);
    if (err != ESP_OK) { goto fail; }
    if (xTaskCreate(rx_task, "proto_uart_rx", 4096, NULL, 6, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.active = true;
    s_snapshot.baud_rate = config->baud_rate;
    portEXIT_CRITICAL(&s_snapshot_lock);
    ESP_LOGI(TAG, "UART1 started: TX GPIO%d, RX GPIO%d, %d baud 8N1",
             PROTO_PIN_UART_TX, PROTO_PIN_UART_RX, config->baud_rate);
    return ESP_OK;

fail:
    if (installed) { uart_driver_delete(PROTO_UART_PORT); }
    vQueueDelete(s_frames);
    vSemaphoreDelete(s_tx_lock);
    s_frames = NULL;
    s_tx_lock = NULL;
    s_events = NULL;
    return err;
}

esp_err_t proto_uart_send(const uint8_t *data, size_t length)
{
    if (!data || length == 0 || length > PROTO_UART_FRAME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!proto_uart_get_snapshot().active) { return ESP_ERR_INVALID_STATE; }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        add_counter(&s_snapshot.tx_failures, 1);
        return ESP_ERR_TIMEOUT;
    }
    const int64_t deadline = esp_timer_get_time() + PROTO_UART_TX_TIMEOUT_US;
    size_t sent = 0;
    esp_err_t err = ESP_OK;
    while (sent < length && esp_timer_get_time() < deadline) {
        const int count = uart_tx_chars(PROTO_UART_PORT, (const char *)data + sent,
                                        (uint32_t)(length - sent));
        if (count < 0) { err = ESP_FAIL; break; }
        sent += (size_t)count;
        if (sent < length) { vTaskDelay(1); }
    }
    const int64_t remaining_us = deadline - esp_timer_get_time();
    if (err == ESP_OK) {
        err = (sent != length || remaining_us <= 0) ? ESP_ERR_TIMEOUT :
              uart_wait_tx_done(PROTO_UART_PORT, pdMS_TO_TICKS((remaining_us + 999) / 1000));
    }
    add_counter(&s_snapshot.tx_bytes, (uint32_t)sent);
    add_counter(err == ESP_OK ? &s_snapshot.tx_completed : &s_snapshot.tx_failures, 1);
    xSemaphoreGive(s_tx_lock);
    return err;
}

esp_err_t proto_uart_receive(proto_uart_frame_t *frame)
{
    if (!frame) { return ESP_ERR_INVALID_ARG; }
    if (!proto_uart_get_snapshot().active) { return ESP_ERR_INVALID_STATE; }
    return xQueueReceive(s_frames, frame, 0) == pdTRUE ? ESP_OK : ESP_ERR_NOT_FOUND;
}

proto_uart_snapshot_t proto_uart_get_snapshot(void)
{
    proto_uart_snapshot_t snapshot;
    portENTER_CRITICAL(&s_snapshot_lock);
    snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_snapshot_lock);
    return snapshot;
}
