#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_display.h"
#include "proto_ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool display_active;
    proto_ui_screen_t screen;
    uint32_t inputs;           /* encoder or console inputs handled */
    uint32_t frames;           /* screens rendered */
    uint32_t flush_errors;
    uint32_t last_frame_ms;    /* render + flush time of the last frame */
    uint32_t max_frame_ms;
} proto_ui_runtime_stats_t;

/* Starts the UI task, the encoder and (with PROTO_UI_AUTOSTART) the display. Idempotent.
 * Nothing here blocks or delays a communication task. */
esp_err_t proto_ui_runtime_start(void);
esp_err_t proto_ui_runtime_display_start(void);
/* Injects an input as if the encoder produced it (console 'ui press'). */
bool proto_ui_runtime_post_input(proto_ui_input_t input);
proto_ui_runtime_stats_t proto_ui_runtime_stats(void);
/* Copies up to max_rows rows of the last rendered screen as text. Returns rows copied. */
size_t proto_ui_runtime_read_screen(char rows[][32], size_t max_rows);
/* Shows a check pattern for a few seconds (4 corner squares and a border). */
void proto_ui_runtime_test_pattern(void);

#ifdef __cplusplus
}
#endif
