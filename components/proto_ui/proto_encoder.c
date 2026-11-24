#include "proto_encoder.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "proto_board.h"
#include "proto_encoder_decode.h"
#include "proto_ui_model.h"

static const char *TAG = "encoder";
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile bool s_invert =
#ifdef CONFIG_PROTO_ENCODER_INVERT
    true;
#else
    false;
#endif
static volatile proto_encoder_stats_t s_stats;
static volatile uint8_t s_steps = CONFIG_PROTO_ENCODER_STEPS_PER_DETENT;

static void edge_isr(void *arg)
{
    (void)arg;
    s_stats.isr_edges++;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    if (woken) { portYIELD_FROM_ISR(woken); }
}

static void post(proto_ui_input_t input)
{
    const uint8_t value = (uint8_t)input;
    (void)xQueueSend(s_queue, &value, 0);
}

static void encoder_task(void *context)
{
    (void)context;
    proto_quad_t quad = {0};
    proto_button_t button = {0};
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));    /* edge wake-up, or 5 ms poll for the button */
        quad.steps_per_detent = s_steps;
        const int detent = proto_quad_update(&quad, gpio_get_level(PROTO_PIN_ENCODER_A) != 0,
                                             gpio_get_level(PROTO_PIN_ENCODER_B) != 0);
        s_stats.valid_transitions = quad.valid_transitions;
        s_stats.missed_edges = quad.missed_edges;
        if (detent != 0) {
            const int dir = s_invert ? -detent : detent;
            s_stats.detents++;
            post(dir > 0 ? PROTO_UI_IN_CW : PROTO_UI_IN_CCW);
        }
        const proto_button_event_t ev = proto_button_update(&button, gpio_get_level(PROTO_PIN_ENCODER_PUSH) == 0,
                                                            (uint64_t)esp_timer_get_time());
        if (ev == PROTO_BUTTON_SHORT) { s_stats.presses++; post(PROTO_UI_IN_SHORT); }
        if (ev == PROTO_BUTTON_LONG) { s_stats.long_presses++; post(PROTO_UI_IN_LONG); }
    }
}

esp_err_t proto_encoder_start(QueueHandle_t input_queue)
{
    if (s_task) { return ESP_OK; }
    if (!input_queue) { return ESP_ERR_INVALID_ARG; }
    s_queue = input_queue;
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << PROTO_PIN_ENCODER_A) | (1ULL << PROTO_PIN_ENCODER_B) | (1ULL << PROTO_PIN_ENCODER_PUSH),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) { return err; }
    if (xTaskCreate(encoder_task, "proto_encoder", 3072, NULL, 7, &s_task) != pdPASS) { return ESP_ERR_NO_MEM; }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { return err; }
    gpio_set_intr_type(PROTO_PIN_ENCODER_A, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(PROTO_PIN_ENCODER_B, GPIO_INTR_ANYEDGE);
    err = gpio_isr_handler_add(PROTO_PIN_ENCODER_A, edge_isr, NULL);
    if (err == ESP_OK) { err = gpio_isr_handler_add(PROTO_PIN_ENCODER_B, edge_isr, NULL); }
    if (err != ESP_OK) { return err; }
    s_stats.started = true;
    ESP_LOGI(TAG, "Encoder ready: A GPIO%d, B GPIO%d, push GPIO%d", PROTO_PIN_ENCODER_A, PROTO_PIN_ENCODER_B,
             PROTO_PIN_ENCODER_PUSH);
    return ESP_OK;
}

void proto_encoder_set_invert(bool invert)
{
    s_invert = invert;
}

proto_encoder_stats_t proto_encoder_stats(void)
{
    proto_encoder_stats_t out;
    out.detents = s_stats.detents;
    out.presses = s_stats.presses;
    out.long_presses = s_stats.long_presses;
    out.isr_edges = s_stats.isr_edges;
    out.valid_transitions = s_stats.valid_transitions;
    out.missed_edges = s_stats.missed_edges;
    out.steps_per_detent = s_steps;
    out.started = s_stats.started;
    out.invert = s_invert;
    return out;
}

void proto_encoder_set_steps(uint8_t steps)
{
    if (steps >= 1 && steps <= 4) { s_steps = steps; }
}

void proto_encoder_read_levels(int *a, int *b, int *sw)
{
    if (a) { *a = gpio_get_level(PROTO_PIN_ENCODER_A); }
    if (b) { *b = gpio_get_level(PROTO_PIN_ENCODER_B); }
    if (sw) { *sw = gpio_get_level(PROTO_PIN_ENCODER_PUSH); }
}
