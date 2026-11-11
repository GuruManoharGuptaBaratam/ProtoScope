#include "proto_can.h"

#include <string.h>

#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "proto_board.h"

#define PROTO_CAN_TX_QUEUE_LEN 8
#define PROTO_CAN_RX_QUEUE_LEN 32
#define PROTO_CAN_APP_QUEUE_LEN 16
#define PROTO_CAN_TX_TIMEOUT_MS 0 /* never block callers: a full TX queue is rejected and counted */

static const char *TAG = "can";
static QueueHandle_t s_frames;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static proto_can_snapshot_t s_snapshot;

static void add_counter(uint32_t *counter, uint32_t amount)
{
    portENTER_CRITICAL(&s_lock);
    *counter += amount;
    portEXIT_CRITICAL(&s_lock);
}

static bool is_active(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool active = s_snapshot.active;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

static void drain_rx(void)
{
    twai_message_t message;
    while (twai_receive(&message, 0) == ESP_OK) {
        proto_can_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.id = message.identifier;
        frame.extended = message.extd != 0;
        frame.rtr = message.rtr != 0;
        frame.dlc = message.data_length_code > PROTO_CAN_MAX_DLC ? PROTO_CAN_MAX_DLC :
                    message.data_length_code;
        if (!frame.rtr) {
            memcpy(frame.data, message.data, frame.dlc);
        }
        frame.timestamp_us = (uint64_t)esp_timer_get_time();
        add_counter(&s_snapshot.rx_frames, 1);
        if (xQueueSend(s_frames, &frame, 0) != pdTRUE) {
            add_counter(&s_snapshot.rx_queue_drops, 1);
        }
    }
}

static void can_task(void *context)
{
    (void)context;
    for (;;) {
        uint32_t alerts = 0;
        /* Timeouts are normal; the queue is drained on every pass anyway so a
         * coalesced or missed RX alert cannot strand received frames. */
        (void)twai_read_alerts(&alerts, pdMS_TO_TICKS(20));
        drain_rx();
        if (alerts & TWAI_ALERT_TX_SUCCESS) {
            add_counter(&s_snapshot.tx_completed, 1);
        }
        if (alerts & TWAI_ALERT_ERR_PASS) {
            add_counter(&s_snapshot.error_passive, 1);
            ESP_LOGW(TAG, "Error-passive (check wiring, bit rate, termination, other node)");
        }
        if (alerts & TWAI_ALERT_BUS_OFF) {
            add_counter(&s_snapshot.bus_off, 1);
            ESP_LOGW(TAG, "Bus-off; starting automatic recovery");
            (void)twai_initiate_recovery();
        }
        if (alerts & TWAI_ALERT_BUS_RECOVERED) {
            add_counter(&s_snapshot.recoveries, 1);
            const esp_err_t err = twai_start();
            ESP_LOGI(TAG, "Bus recovered; controller restart: %s", esp_err_to_name(err));
        }
    }
}

static twai_timing_config_t timing_for(uint32_t bit_rate)
{
    switch (bit_rate) {
    case 125000: { const twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS(); return t; }
    case 250000: { const twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS(); return t; }
    case 1000000: { const twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS(); return t; }
    default: { const twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS(); return t; }
    }
}

esp_err_t proto_can_start(const proto_can_config_t *config)
{
    if (!config || !proto_can_bitrate_supported(config->bit_rate) ||
        (unsigned)config->mode > (unsigned)PROTO_CAN_MODE_SELF_TEST) {
        return ESP_ERR_INVALID_ARG;
    }
    if (is_active()) { return ESP_ERR_INVALID_STATE; }

    s_frames = xQueueCreate(PROTO_CAN_APP_QUEUE_LEN, sizeof(proto_can_frame_t));
    if (!s_frames) { return ESP_ERR_NO_MEM; }

    twai_mode_t mode = TWAI_MODE_NORMAL;
    if (config->mode == PROTO_CAN_MODE_LISTEN_ONLY) { mode = TWAI_MODE_LISTEN_ONLY; }
    if (config->mode == PROTO_CAN_MODE_SELF_TEST) { mode = TWAI_MODE_NO_ACK; }

    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(PROTO_PIN_TWAI_TX,
                                                                PROTO_PIN_TWAI_RX, mode);
    general.tx_queue_len = PROTO_CAN_TX_QUEUE_LEN;
    general.rx_queue_len = PROTO_CAN_RX_QUEUE_LEN;
    general.alerts_enabled = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_ERR_PASS |
                             TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
    const twai_timing_config_t timing = timing_for(config->bit_rate);
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    bool installed = false;
    esp_err_t err = twai_driver_install(&general, &timing, &filter);
    if (err != ESP_OK) { goto fail; }
    installed = true;
    err = twai_start();
    if (err != ESP_OK) { goto fail; }
    if (xTaskCreate(can_task, "proto_can", 4096, NULL, 6, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        (void)twai_stop();
        goto fail;
    }
    portENTER_CRITICAL(&s_lock);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.active = true;
    s_snapshot.bit_rate = config->bit_rate;
    s_snapshot.mode = config->mode;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "TWAI started: TX GPIO%d, RX GPIO%d, %u bit/s, mode %d",
             PROTO_PIN_TWAI_TX, PROTO_PIN_TWAI_RX, (unsigned)config->bit_rate,
             (int)config->mode);
    return ESP_OK;

fail:
    if (installed) { (void)twai_driver_uninstall(); }
    vQueueDelete(s_frames);
    s_frames = NULL;
    return err;
}

esp_err_t proto_can_send(const proto_can_frame_t *frame)
{
    if (!proto_can_frame_valid(frame)) { return ESP_ERR_INVALID_ARG; }
    if (!is_active()) { return ESP_ERR_INVALID_STATE; }
    portENTER_CRITICAL(&s_lock);
    const proto_can_mode_t mode = s_snapshot.mode;
    portEXIT_CRITICAL(&s_lock);
    if (mode == PROTO_CAN_MODE_LISTEN_ONLY) { return ESP_ERR_NOT_SUPPORTED; }

    twai_message_t message;
    memset(&message, 0, sizeof(message));
    message.identifier = frame->id;
    message.extd = frame->extended ? 1 : 0;
    message.rtr = frame->rtr ? 1 : 0;
    message.self = (mode == PROTO_CAN_MODE_SELF_TEST) ? 1 : 0;
    message.data_length_code = frame->dlc;
    if (!frame->rtr) {
        memcpy(message.data, frame->data, frame->dlc);
    }
    const esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(PROTO_CAN_TX_TIMEOUT_MS));
    add_counter(err == ESP_OK ? &s_snapshot.tx_requested : &s_snapshot.tx_rejected, 1);
    return err;
}

esp_err_t proto_can_receive(proto_can_frame_t *frame)
{
    if (!frame) { return ESP_ERR_INVALID_ARG; }
    if (!is_active()) { return ESP_ERR_INVALID_STATE; }
    return xQueueReceive(s_frames, frame, 0) == pdTRUE ? ESP_OK : ESP_ERR_NOT_FOUND;
}

proto_can_snapshot_t proto_can_get_snapshot(void)
{
    proto_can_snapshot_t snapshot;
    portENTER_CRITICAL(&s_lock);
    snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
    if (!snapshot.active) { return snapshot; }

    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        switch (status.state) {
        case TWAI_STATE_RUNNING: snapshot.state = PROTO_CAN_STATE_RUNNING; break;
        case TWAI_STATE_BUS_OFF: snapshot.state = PROTO_CAN_STATE_BUS_OFF; break;
        case TWAI_STATE_RECOVERING: snapshot.state = PROTO_CAN_STATE_RECOVERING; break;
        default: snapshot.state = PROTO_CAN_STATE_STOPPED; break;
        }
        snapshot.tx_error_counter = status.tx_error_counter;
        snapshot.rx_error_counter = status.rx_error_counter;
        snapshot.tx_failed = status.tx_failed_count;
        snapshot.rx_missed = status.rx_missed_count;
        snapshot.rx_overrun = status.rx_overrun_count;
        snapshot.arb_lost = status.arb_lost_count;
        snapshot.bus_errors = status.bus_error_count;
    }
    return snapshot;
}
