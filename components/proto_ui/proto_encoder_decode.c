#include "proto_encoder_decode.h"

/* Index = (previous << 2) | current, Gray-code order 00,01,11,10. +1 clockwise. */
static const int8_t k_transition[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

int proto_quad_update(proto_quad_t *q, bool a, bool b)
{
    if (!q) { return 0; }
    const uint8_t state = (uint8_t)((a ? 2u : 0u) | (b ? 1u : 0u));
    if (!q->initialized) {
        q->initialized = true;
        q->previous = state;
        q->accumulator = 0;
        return 0;
    }
    if (state == q->previous) { return 0; }
    int step = k_transition[(q->previous << 2) | state];
    q->previous = state;
    if (step != 0) {
        q->valid_transitions++;
    } else {
        /* Both lines changed between two readings. Keep going in the same direction. */
        q->missed_edges++;
        if (q->accumulator == 0) { return 0; }
        step = q->accumulator > 0 ? 2 : -2;
    }
    const int threshold = (q->steps_per_detent >= 1 && q->steps_per_detent <= 4) ?
                          q->steps_per_detent : PROTO_ENCODER_STEPS_PER_DETENT;
    int total = q->accumulator + step;
    if (total >= threshold) { q->accumulator = 0; return +1; }
    if (total <= -threshold) { q->accumulator = 0; return -1; }
    q->accumulator = (int8_t)total;
    return 0;
}

proto_button_event_t proto_button_update(proto_button_t *b, bool pressed, uint64_t now_us)
{
    if (!b) { return PROTO_BUTTON_NONE; }
    if (!b->initialized) {
        b->initialized = true;
        b->last_raw = pressed;
        b->raw_changed_us = now_us;
        b->stable_pressed = pressed;
        b->pressed_since_us = now_us;
        b->long_reported = pressed;      /* a switch already held at boot never fires */
        return PROTO_BUTTON_NONE;
    }
    if (pressed != b->last_raw) {
        b->last_raw = pressed;
        b->raw_changed_us = now_us;
    }
    if (b->last_raw != b->stable_pressed && now_us - b->raw_changed_us >= PROTO_BUTTON_DEBOUNCE_US) {
        b->stable_pressed = b->last_raw;
        if (b->stable_pressed) {
            b->pressed_since_us = b->raw_changed_us;
            b->long_reported = false;
        } else {
            const bool was_long = b->long_reported;
            b->long_reported = false;
            if (!was_long) { return PROTO_BUTTON_SHORT; }
            return PROTO_BUTTON_NONE;
        }
    }
    if (b->stable_pressed && !b->long_reported && now_us - b->pressed_since_us >= PROTO_BUTTON_LONG_US) {
        b->long_reported = true;
        return PROTO_BUTTON_LONG;
    }
    return PROTO_BUTTON_NONE;
}
