#pragma once

/* 0.96 inch 4-pin OLED, 128x64, SSD1306, I2C (Phase 9). Wiring (module pins GND VCC SCL SDA):
 *   SDA -> GPIO14, SCL -> GPIO10 on I2C_NUM_1, VCC -> 3V3, GND -> GND.
 * This is a dedicated bus: the external I2C port (GPIO8/GPIO9, I2C_NUM_0) is never used by
 * the display, so a 0x3C device on the external port cannot clash with it.
 * The UI draws into a colour-index canvas; this layer packs it into the 1-bit page format. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_DISPLAY_WIDTH 128
#define PROTO_DISPLAY_HEIGHT 64

typedef struct {
    bool active;
    const char *name;              /* "SSD1306" */
    uint8_t i2c_address;           /* 0x3C or 0x3D once found */
    uint16_t width;
    uint16_t height;
    bool flipped;
    uint32_t flushes;
    uint32_t flush_errors;
    uint32_t consecutive_errors;
    uint32_t starts;               /* successful (re)initializations */
} proto_display_info_t;

/* Probes 0x3C then 0x3D and initializes the panel. ESP_ERR_NOT_FOUND when nothing
 * answers (module not connected). Idempotent while active. */
esp_err_t proto_display_start(void);
/* Releases the I2C bus so a later start can probe again (used after repeated errors). */
void proto_display_stop(void);
proto_display_info_t proto_display_info(void);
/* Canvas must be exactly 128x64. */
esp_err_t proto_display_flush(const proto_canvas_t *canvas);
/* Rotate the picture 180 degrees (some modules are mounted upside down). */
esp_err_t proto_display_flip(bool flipped);

#ifdef __cplusplus
}
#endif
