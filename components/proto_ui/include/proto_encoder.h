#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EC11 rotary encoder with push switch: A = GPIO1, B = GPIO2, push = GPIO6, active-low
 * with the ESP's pull-ups enabled. Posts proto_ui_input_t values (uint8_t) to the queue. */
esp_err_t proto_encoder_start(QueueHandle_t input_queue);
void proto_encoder_set_invert(bool invert);
typedef struct {
    uint32_t detents;
    uint32_t presses;
    uint32_t long_presses;
    uint32_t isr_edges;             /* A/B edge interrupts seen: 0 while turning = no signal on GPIO1/GPIO2 */
    uint32_t valid_transitions;
    uint32_t missed_edges;
    uint8_t steps_per_detent;
    bool invert;
    bool started;
} proto_encoder_stats_t;
/* 1..4 transitions per click; see PROTO_ENCODER_STEPS_PER_DETENT. */
void proto_encoder_set_steps(uint8_t steps);
/* Live pin levels (1 = high) for the 'ui encoder' diagnostic. */
void proto_encoder_read_levels(int *a, int *b, int *sw);
proto_encoder_stats_t proto_encoder_stats(void);

#ifdef __cplusplus
}
#endif
