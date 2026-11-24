#include "proto_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "proto_can.h"
#include "proto_encoder.h"
#include "proto_i2c.h"
#include "proto_monitor.h"
#include "proto_route_runtime.h"
#include "proto_rs485.h"
#include "proto_spi.h"
#include "proto_storage.h"
#include "proto_uart.h"

#define FRAME_PERIOD_US 250000ull
#define TEST_PATTERN_US 6000000ull
#define DISPLAY_RETRY_US 4000000ull   /* look for an OLED that was plugged in after boot */
#define DISPLAY_ERRORS_BEFORE_RESET 4u
#define SCREEN_ROWS 10

static const char *TAG = "ui";
static QueueHandle_t s_input_queue;
static SemaphoreHandle_t s_mutex;
static proto_ui_t s_ui;
static proto_monitor_t s_status;
static uint8_t s_pixels[PROTO_DISPLAY_WIDTH * PROTO_DISPLAY_HEIGHT];
static proto_canvas_t s_canvas;
static char s_screen[SCREEN_ROWS][32];
static size_t s_screen_rows;
static proto_ui_runtime_stats_t s_stats;
static uint64_t s_test_until_us;
static bool s_started;

/* ---- actions: the few things the UI can really do ------------------------------------------ */
static const char *start_message(esp_err_t err, bool *ok)
{
    *ok = err == ESP_OK;
    if (err == ESP_OK) { return "Started"; }
    if (err == ESP_ERR_INVALID_STATE) { return "Already started"; }
    return "Start failed";
}

static bool action_start(void *context, proto_protocol_t protocol, const proto_ui_params_t *p, const char **message)
{
    (void)context;
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    switch (protocol) {
    case PROTO_PROTOCOL_UART: {
        proto_uart_config_t c = PROTO_UART_CONFIG_DEFAULT();
        c.baud_rate = (int)p->value;
        err = proto_uart_start(&c);
        break;
    }
    case PROTO_PROTOCOL_RS485: {
        proto_rs485_config_t c = PROTO_RS485_CONFIG_DEFAULT();
        c.baud_rate = p->value;
        c.discard_echo = !p->flag;
        err = proto_rs485_start(&c);
        break;
    }
    case PROTO_PROTOCOL_CAN: {
        proto_can_config_t c = PROTO_CAN_CONFIG_DEFAULT();
        c.bit_rate = p->value;
        c.mode = (proto_can_mode_t)p->mode;
        err = proto_can_start(&c);
        break;
    }
    case PROTO_PROTOCOL_I2C: {
        proto_i2c_config_t c = PROTO_I2C_CONFIG_DEFAULT();
        c.frequency_hz = p->value;
        c.internal_pullup = p->flag;
        err = proto_i2c_start(&c);
        break;
    }
    case PROTO_PROTOCOL_SPI: {
        proto_spi_config_t c = PROTO_SPI_CONFIG_DEFAULT();
        c.mode = p->mode;
        c.clock_hz = p->value;
        err = proto_spi_start(&c);
        break;
    }
    case PROTO_PROTOCOL_COUNT:
        break;
    }
    bool ok;
    *message = start_message(err, &ok);
    return ok;
}

static bool action_route(void *context, uint8_t id, bool enable, const char **message)
{
    (void)context;
    bool ok = true;
    proto_route_runtime_lock();
    proto_route_engine_t *e = proto_route_runtime_engine();
    if (enable) {
        const proto_route_check_t check = proto_route_engine_enable(e, id);
        ok = check == PROTO_ROUTE_OK;
        *message = ok ? "Route enabled" : proto_route_check_name(check);
    } else {
        proto_route_engine_disable(e, id);
        *message = "Route disabled";
    }
    proto_route_runtime_unlock();
    return ok;
}

static bool action_log(void *context, bool start, const char **message)
{
    (void)context;
    if (!start) {
        proto_storage_log_stop();
        *message = "Logging stopped";
        return true;
    }
    switch (proto_storage_log_start()) {
    case PROTO_LOG_STATE_RUNNING: *message = "Logging started"; return true;
    case PROTO_LOG_STATE_NO_CARD: *message = "No SD card"; return false;
    case PROTO_LOG_STATE_FULL: *message = "SD full"; return false;
    default: *message = "SD error"; return false;
    }
}

/* ---- display and frame loop ---------------------------------------------------------------- */
static void draw_test_pattern(proto_canvas_t *c, const proto_display_info_t *d)
{
    proto_canvas_clear(c, PROTO_CANVAS_BG);
    proto_canvas_rect(c, 0, 0, c->width, c->height, PROTO_CANVAS_FG);
    proto_canvas_fill_rect(c, 0, 0, 6, 6, PROTO_CANVAS_FG);
    proto_canvas_fill_rect(c, c->width - 6, 0, 6, 6, PROTO_CANVAS_FG);
    proto_canvas_fill_rect(c, 0, c->height - 6, 6, 6, PROTO_CANVAS_FG);
    proto_canvas_fill_rect(c, c->width - 6, c->height - 6, 6, 6, PROTO_CANVAS_FG);
    char line[32];
    snprintf(line, sizeof(line), "%s %ux%u", d->name ? d->name : "none", (unsigned)c->width, (unsigned)c->height);
    proto_canvas_text(c, 10, 14, line, PROTO_CANVAS_FG, PROTO_CANVAS_BG);
    snprintf(line, sizeof(line), "I2C 0x%02X", (unsigned)d->i2c_address);
    proto_canvas_text(c, 10, 26, line, PROTO_CANVAS_FG, PROTO_CANVAS_BG);
    proto_canvas_text(c, 10, 38, "4 corners + border?", PROTO_CANVAS_FG, PROTO_CANVAS_BG);
}

static void run_frame(uint64_t now)
{
    const int64_t t0 = esp_timer_get_time();
    const proto_display_info_t d = proto_display_info();
    proto_canvas_init(&s_canvas, PROTO_DISPLAY_WIDTH, PROTO_DISPLAY_HEIGHT, s_pixels);

    proto_monitor_collect(&s_status);
    snprintf(s_status.display_name, sizeof(s_status.display_name), "%s", d.name ? d.name : "none");
    s_status.display_active = d.active;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (now < s_test_until_us) { draw_test_pattern(&s_canvas, &d); }
    else { proto_ui_render(&s_ui, &s_status, &s_canvas, now); }
    s_screen_rows = proto_canvas_rows(&s_canvas) < SCREEN_ROWS ? proto_canvas_rows(&s_canvas) : SCREEN_ROWS;
    for (size_t r = 0; r < s_screen_rows; ++r) { (void)proto_canvas_read_row(&s_canvas, (uint16_t)r, s_screen[r], sizeof(s_screen[r])); }
    s_stats.screen = s_ui.screen;
    s_stats.inputs = s_ui.input_count;
    xSemaphoreGive(s_mutex);

    if (d.active && proto_display_flush(&s_canvas) != ESP_OK) {
        s_stats.flush_errors++;
        if (proto_display_info().consecutive_errors >= DISPLAY_ERRORS_BEFORE_RESET) {
            ESP_LOGW(TAG, "OLED stopped answering; will look for it again");
            proto_display_stop();      /* the retry below re-probes and re-initializes it */
        }
    }
    const uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    s_stats.frames++;
    s_stats.last_frame_ms = ms;
    if (ms > s_stats.max_frame_ms) { s_stats.max_frame_ms = ms; }
}

static void ui_task(void *context)
{
    (void)context;
    uint64_t last_frame = 0, last_display_try = 0;
    for (;;) {
        uint8_t raw;
        bool got = false;
        if (xQueueReceive(s_input_queue, &raw, pdMS_TO_TICKS(20)) == pdTRUE) {
            got = true;
            proto_monitor_collect(&s_status);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            do { proto_ui_input(&s_ui, (proto_ui_input_t)raw, &s_status, (uint64_t)esp_timer_get_time()); }
            while (xQueueReceive(s_input_queue, &raw, 0) == pdTRUE);
            xSemaphoreGive(s_mutex);
        }
        const uint64_t now = (uint64_t)esp_timer_get_time();
#ifdef CONFIG_PROTO_UI_AUTOSTART
        if (!proto_display_info().active && now - last_display_try >= DISPLAY_RETRY_US) {
            last_display_try = now;
            (void)proto_display_start();                 /* quiet: a missing OLED is not an error */
        }
#endif
        if (got || now - last_frame >= FRAME_PERIOD_US) {
            run_frame(now);
            last_frame = now;
        }
    }
}

esp_err_t proto_ui_runtime_display_start(void)
{
    const esp_err_t err = proto_display_start();
    if (err == ESP_OK) { s_stats.display_active = true; }
    return err;
}

esp_err_t proto_ui_runtime_start(void)
{
    if (s_started) { return ESP_OK; }
    s_input_queue = xQueueCreate(16, sizeof(uint8_t));
    s_mutex = xSemaphoreCreateMutex();
    if (!s_input_queue || !s_mutex) { return ESP_ERR_NO_MEM; }
    const proto_ui_actions_t actions = { NULL, action_start, action_route, action_log };
    proto_ui_init(&s_ui, &actions);
    if (xTaskCreate(ui_task, "proto_ui", 6144, NULL, 3, NULL) != pdPASS) { return ESP_ERR_NO_MEM; }
    s_started = true;
    s_stats.started = true;
#ifdef CONFIG_PROTO_UI_AUTOSTART
    esp_err_t err = proto_encoder_start(s_input_queue);
    if (err != ESP_OK) { ESP_LOGW(TAG, "Encoder not started: %s", esp_err_to_name(err)); }
    err = proto_ui_runtime_display_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No OLED found yet (%s): UI keeps running and checks again every few seconds", esp_err_to_name(err));
    }
#else
    ESP_LOGI(TAG, "UI autostart disabled; use 'display start' and 'ui' commands");
#endif
    return ESP_OK;
}

bool proto_ui_runtime_post_input(proto_ui_input_t input)
{
    if (!s_input_queue) { return false; }
    const uint8_t value = (uint8_t)input;
    return xQueueSend(s_input_queue, &value, 0) == pdTRUE;
}

proto_ui_runtime_stats_t proto_ui_runtime_stats(void)
{
    proto_ui_runtime_stats_t out = s_stats;
    out.display_active = proto_display_info().active;
    return out;
}

size_t proto_ui_runtime_read_screen(char rows[][32], size_t max_rows)
{
    if (!s_mutex) { return 0; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const size_t n = s_screen_rows < max_rows ? s_screen_rows : max_rows;
    for (size_t i = 0; i < n; ++i) { memcpy(rows[i], s_screen[i], 32); }
    xSemaphoreGive(s_mutex);
    return n;
}

void proto_ui_runtime_test_pattern(void)
{
    s_test_until_us = (uint64_t)esp_timer_get_time() + TEST_PATTERN_US;
    (void)proto_ui_runtime_post_input(PROTO_UI_IN_NONE);   /* wake the task so the pattern shows now */
}
