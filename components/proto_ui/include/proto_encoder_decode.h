#pragma once

/* Rotary encoder (EC11) quadrature decoder and push-button debouncer. Pure C. */

#include <stdbool.h>
#include <stdint.h>

#define PROTO_ENCODER_STEPS_PER_DETENT 4   /* typical EC11: 4 transitions per click */
#define PROTO_BUTTON_DEBOUNCE_US 20000ull
#define PROTO_BUTTON_LONG_US 700000ull

typedef struct {
    uint8_t previous;            /* last A/B state (2 bits) */
    int8_t accumulator;          /* transitions since the last emitted detent */
    bool initialized;
    uint8_t steps_per_detent;    /* 1..4; 0 = PROTO_ENCODER_STEPS_PER_DETENT. EC11 types differ:
                                    most 20-click knobs need 4, 30-click knobs (15 pulses) need 2 */
    uint32_t valid_transitions;  /* diagnostics: single-step changes seen */
    uint32_t missed_edges;       /* diagnostics: both lines changed between two readings */
} proto_quad_t;

/* Feed the current A and B levels. Returns +1 or -1 when a full detent was turned
 * (clockwise = +1), otherwise 0. A skipped edge (both lines changed between two readings,
 * typical of a bouncing contact) counts as two steps in the direction already being turned,
 * and is ignored when the knob was not moving. */
int proto_quad_update(proto_quad_t *quad, bool a, bool b);

typedef enum {
    PROTO_BUTTON_NONE = 0,
    PROTO_BUTTON_SHORT,   /* released before the long-press time */
    PROTO_BUTTON_LONG,    /* held for the long-press time (reported once, while held) */
} proto_button_event_t;

typedef struct {
    bool stable_pressed;
    bool last_raw;
    uint64_t raw_changed_us;
    uint64_t pressed_since_us;
    bool long_reported;
    bool initialized;
} proto_button_t;

/* pressed = true while the switch is closed. Call regularly (>= every 10 ms). */
proto_button_event_t proto_button_update(proto_button_t *button, bool pressed, uint64_t now_us);
