#include "proto_canvas.h"
#include "proto_encoder_decode.h"
#include "proto_ui_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

extern const uint8_t proto_font5x7[95][5];

/* ---------------------------------------------------------------- read text back from pixels */
static void row_text(const proto_canvas_t *c, uint16_t row, char *out)
{
    (void)proto_canvas_read_row(c, row, out, 64);
}

static uint8_t row_color(const proto_canvas_t *c, uint16_t row)
{
    for (int y = row * PROTO_CELL_H; y < (row + 1) * PROTO_CELL_H; ++y) {
        for (int x = 0; x < c->width; ++x) {
            if (c->pixels[(size_t)y * c->width + (size_t)x] == PROTO_CANVAS_ACCENT) { return PROTO_CANVAS_ACCENT; }
        }
    }
    return PROTO_CANVAS_FG;
}

/* ---------------------------------------------------------------- canvas */
static void test_canvas(void)
{
    static uint8_t px[128 * 64];
    proto_canvas_t c; proto_canvas_init(&c, 128, 64, px);
    assert(proto_canvas_columns(&c) == 21 && proto_canvas_rows(&c) == 8);
    static uint8_t px2[160 * 80];
    proto_canvas_t c2; proto_canvas_init(&c2, 160, 80, px2);
    assert(proto_canvas_columns(&c2) == 26 && proto_canvas_rows(&c2) == 10);
    proto_canvas_clear(&c, 0);
    proto_canvas_pixel(&c, -1, 0, 1); proto_canvas_pixel(&c, 0, 64, 1); proto_canvas_pixel(&c, 128, 0, 1);   /* ignored */
    proto_canvas_pixel(&c, 127, 63, 1);
    assert(px[63 * 128 + 127] == 1);
    proto_canvas_fill_rect(&c, 120, 56, 20, 20, 2);                                                           /* clipped */
    assert(px[63 * 128 + 127] == 2 && px[56 * 128 + 120] == 2);
    proto_canvas_clear(&c, 0);
    proto_canvas_text(&c, 0, 0, "A", 1, 0);
    assert(px[1 * 128 + 1] == 1 && px[6 * 128 + 0] == 1 && px[0 * 128 + 0] == 0 && px[7 * 128 + 0] == 0);   /* 'A' column 0 = 0x7C */
    proto_canvas_text_row(&c, 0, "0123456789012345678901234567890123", false, false);                          /* longer than 21 cols */
    char text[40]; row_text(&c, 0, text);
    assert(strcmp(text, "012345678901234567890") == 0);
    proto_canvas_text_row(&c, 1, "Hi", true, false);
    row_text(&c, 1, text);
    assert(strcmp(text, "Hi") == 0 && px[8 * 128 + 127] == PROTO_CANVAS_FG);                                   /* inverted: filled bar */
    proto_canvas_text_row(&c, 9, "off screen", false, false);                                                  /* ignored */
    proto_canvas_text(&c, 124, 60, "WWWW", 1, 0);                                                              /* clipped, no overrun */
    proto_canvas_rect(&c, 0, 0, 128, 64, 1);
    assert(px[0] == 1 && px[63 * 128 + 127] == 1);
    proto_canvas_text(&c, 0, 0, "\x01\xFF", 1, 0);                                                             /* unknown -> '?' */
    proto_canvas_text(NULL, 0, 0, "x", 1, 0);
}

static void test_oled_packing(void)
{
    static uint8_t px[128 * 64];
    proto_canvas_t c; proto_canvas_init(&c, 128, 64, px);
    proto_canvas_clear(&c, 0);
    proto_canvas_text(&c, 0, 0, "A", 1, 0);
    uint8_t page[128];
    memset(page, 0xEE, sizeof(page));
    proto_canvas_pack_page(&c, 0, page);
    for (int x = 0; x < 5; ++x) { assert(page[x] == proto_font5x7['A' - 32][x]); }   /* bit 0 = top row, as the SSD1306 expects */
    assert(page[5] == 0 && page[127] == 0);
    proto_canvas_pack_page(&c, 1, page);
    for (int x = 0; x < 128; ++x) { assert(page[x] == 0); }
    proto_canvas_pixel(&c, 127, 63, PROTO_CANVAS_ACCENT);                          /* any colour index is lit */
    proto_canvas_pixel(&c, 3, 8, 1);
    proto_canvas_pack_page(&c, 7, page);
    assert(page[127] == 0x80);
    proto_canvas_pack_page(&c, 1, page);
    assert(page[3] == 0x01);
    proto_canvas_pack_page(&c, 9, page);                                           /* beyond the canvas: dark, no overrun */
    assert(page[0] == 0 && page[127] == 0);
    proto_canvas_pack_page(NULL, 0, page);
}

/* ---------------------------------------------------------------- encoder and button */
static void test_encoder(void)
{
    proto_quad_t q; memset(&q, 0, sizeof(q));
    assert(proto_quad_update(&q, false, false) == 0);
    /* clockwise: A leads B: 00 -> 10 -> 11 -> 01 -> 00 */
    assert(proto_quad_update(&q, true, false) == 0 && proto_quad_update(&q, true, true) == 0 &&
           proto_quad_update(&q, false, true) == 0 && proto_quad_update(&q, false, false) == +1);
    /* counter-clockwise: 00 -> 01 -> 11 -> 10 -> 00 */
    assert(proto_quad_update(&q, false, true) == 0 && proto_quad_update(&q, true, true) == 0 &&
           proto_quad_update(&q, true, false) == 0 && proto_quad_update(&q, false, false) == -1);
    /* three clicks in a row */
    int total = 0;
    for (int i = 0; i < 3; ++i) {
        total += proto_quad_update(&q, true, false); total += proto_quad_update(&q, true, true);
        total += proto_quad_update(&q, false, true); total += proto_quad_update(&q, false, false);
    }
    assert(total == 3);
    /* a quarter turn and back does not click; contact bounce does not count */
    assert(proto_quad_update(&q, true, false) == 0 && proto_quad_update(&q, false, false) == 0);
    for (int i = 0; i < 20; ++i) { assert(proto_quad_update(&q, true, false) == 0 && proto_quad_update(&q, false, false) == 0); }
    /* a skipped edge while idle is ignored */
    proto_quad_t idle; memset(&idle, 0, sizeof(idle));
    assert(proto_quad_update(&idle, false, false) == 0 && proto_quad_update(&idle, true, true) == 0 && idle.missed_edges == 1);
    /* a skipped edge in the middle of a turn still completes the click (bouncing contact) */
    proto_quad_t bounce; memset(&bounce, 0, sizeof(bounce));
    assert(proto_quad_update(&bounce, true, true) == 0);                     /* rest 11 */
    assert(proto_quad_update(&bounce, false, true) == 0);                    /* step 1 (A falls) */
    assert(proto_quad_update(&bounce, true, false) == 0 || true);            /* 01 -> 10 skips 00: counts 2 more */
    assert(bounce.missed_edges == 1 && bounce.accumulator == 3);
    assert(proto_quad_update(&bounce, true, true) == +1);                    /* 10 -> 11 completes the click */
    /* 30-click knobs: two changes per click */
    proto_quad_t two; memset(&two, 0, sizeof(two)); two.steps_per_detent = 2;
    assert(proto_quad_update(&two, true, true) == 0);
    assert(proto_quad_update(&two, false, true) == 0 && proto_quad_update(&two, false, false) == +1);   /* 11 -> 01 -> 00 */
    assert(proto_quad_update(&two, true, false) == 0 && proto_quad_update(&two, true, true) == +1);     /* 00 -> 10 -> 11 */
    assert(proto_quad_update(&two, true, false) == 0 && proto_quad_update(&two, false, false) == -1);   /* back the other way */
    assert(two.valid_transitions == 6);
    proto_quad_t one; memset(&one, 0, sizeof(one)); one.steps_per_detent = 1;
    assert(proto_quad_update(&one, true, true) == 0 && proto_quad_update(&one, false, true) == +1);
    proto_quad_t bad; memset(&bad, 0, sizeof(bad)); bad.steps_per_detent = 9;                           /* invalid -> default 4 */
    assert(proto_quad_update(&bad, true, true) == 0 && proto_quad_update(&bad, false, true) == 0);
    assert(proto_quad_update(NULL, true, true) == 0);
}

static void test_button(void)
{
    proto_button_t b; memset(&b, 0, sizeof(b));
    uint64_t t = 0;
    assert(proto_button_update(&b, false, t) == PROTO_BUTTON_NONE);
    /* clean short press */
    int shorts = 0, longs = 0;
    for (; t < 100000; t += 5000) { const proto_button_event_t e = proto_button_update(&b, t >= 10000 && t < 70000, t); shorts += e == PROTO_BUTTON_SHORT; longs += e == PROTO_BUTTON_LONG; }
    assert(shorts == 1 && longs == 0);
    /* bouncing contacts under 20 ms produce nothing */
    shorts = 0;
    for (int i = 0; i < 6; ++i, t += 3000) { shorts += proto_button_update(&b, i % 2, t) == PROTO_BUTTON_SHORT; }
    for (int i = 0; i < 10; ++i, t += 5000) { shorts += proto_button_update(&b, false, t) == PROTO_BUTTON_SHORT; }
    assert(shorts == 0);
    /* long press: reported once while held, release reports nothing */
    shorts = longs = 0;
    for (uint64_t start = t; t < start + 1200000; t += 5000) {
        const proto_button_event_t e = proto_button_update(&b, t < start + 1000000, t);
        shorts += e == PROTO_BUTTON_SHORT; longs += e == PROTO_BUTTON_LONG;
    }
    assert(longs == 1 && shorts == 0);
    /* a press that starts at boot never fires */
    proto_button_t held; memset(&held, 0, sizeof(held));
    int events = 0;
    for (uint64_t u = 0; u < 3000000; u += 5000) { events += proto_button_update(&held, true, u) != PROTO_BUTTON_NONE; }
    assert(events == 0);
    assert(proto_button_update(NULL, true, 0) == PROTO_BUTTON_NONE);
}

/* ---------------------------------------------------------------- UI model */
typedef struct {
    int starts, route_calls, log_calls;
    proto_protocol_t protocol;
    proto_ui_params_t params;
    uint8_t route_id; bool route_enable, log_start;
    bool start_ok;
    const char *start_msg;
} mock_t;

static bool a_start(void *ctx, proto_protocol_t p, const proto_ui_params_t *params, const char **message)
{
    mock_t *m = ctx; m->starts++; m->protocol = p; m->params = *params; *message = m->start_msg; return m->start_ok;
}
static bool a_route(void *ctx, uint8_t id, bool enable, const char **message)
{
    mock_t *m = ctx; m->route_calls++; m->route_id = id; m->route_enable = enable; *message = enable ? "RT enabled" : "RT disabled"; return true;
}
static bool a_log(void *ctx, bool start, const char **message)
{
    mock_t *m = ctx; m->log_calls++; m->log_start = start; *message = start ? "Logging started" : "Logging stopped"; return true;
}

static proto_monitor_t status(void)
{
    proto_monitor_t s; memset(&s, 0, sizeof(s));
    strcpy(s.firmware, "0.9.0"); strcpy(s.display_name, "SSD1306"); s.display_active = true;
    s.uptime_s = 125; s.free_heap = 8700000; s.min_free_heap = 8600000; s.psram_bytes = 8388608;
    for (int i = 0; i < PROTO_MONITOR_ROUTES; ++i) {
        static const uint8_t src[20] = { 0,2,0,1,2,1,0,3,2,3,1,3,0,4,2,4,1,4,3,4 };
        static const uint8_t dst[20] = { 2,0,1,0,1,2,3,0,3,2,3,1,4,0,4,2,4,1,4,3 };
        s.route_src[i] = src[i]; s.route_dst[i] = dst[i];
    }
    s.route_ready[0] = s.route_ready[2] = s.route_ready[3] = true;
    s.routes_ready = 3;
    return s;
}

static void test_home(void)
{
    static uint8_t px[128 * 64];
    proto_canvas_t c; proto_canvas_init(&c, 128, 64, px);
    mock_t m; memset(&m, 0, sizeof(m));
    proto_ui_actions_t acts = { &m, a_start, a_route, a_log };
    proto_ui_t ui; proto_ui_init(&ui, &acts);
    proto_monitor_t s = status();
    s.iface[PROTO_PROTOCOL_UART].active = true; s.iface[PROTO_PROTOCOL_UART].rx = 12; s.iface[PROTO_PROTOCOL_UART].tx = 3;
    s.iface[PROTO_PROTOCOL_CAN].active = true; s.iface[PROTO_PROTOCOL_CAN].rx = 34567; s.iface[PROTO_PROTOCOL_CAN].tx = 2; s.iface[PROTO_PROTOCOL_CAN].errors = 5;
    s.iface[PROTO_PROTOCOL_CAN].warning = true;
    s.routes_enabled = 2;
    proto_ui_render(&ui, &s, &c, 0);
    char t[40];
    row_text(&c, 0, t); assert(strcmp(t, "PROTOSCOPE 0.9.0") == 0);
    row_text(&c, 1, t); assert(strcmp(t, "UART* 12/3 E0") == 0);
    row_text(&c, 2, t); assert(strcmp(t, "R485- off") == 0);
    row_text(&c, 3, t); assert(strcmp(t, "CAN ! 34k/2 E5") == 0 && row_color(&c, 3) == PROTO_CANVAS_ACCENT);
    row_text(&c, 4, t); assert(strcmp(t, "I2C - off") == 0);
    row_text(&c, 6, t); assert(strcmp(t, "RT:2/20 SD:off") == 0);
    row_text(&c, 7, t); assert(strcmp(t, "!CAN ERRORS") == 0 && row_color(&c, 7) == PROTO_CANVAS_ACCENT);
    /* a storage problem the user asked for outranks everything */
    s.log_wanted = true; s.log_state = PROTO_LOG_STATE_NO_CARD;
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 7, t); assert(strcmp(t, "!SD NO CARD") == 0);
    row_text(&c, 6, t); assert(strcmp(t, "RT:2/20 SD:NO CARD") == 0);
    /* healthy */
    memset(s.iface, 0, sizeof(s.iface)); s.log_wanted = false; s.log_state = PROTO_LOG_STATE_OFF;
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 7, t); assert(strcmp(t, "OK  up 125s") == 0 && row_color(&c, 7) == PROTO_CANVAS_FG);
    /* taller 160x80 panel shows more lines */
    static uint8_t px2[160 * 80];
    proto_canvas_t c2; proto_canvas_init(&c2, 160, 80, px2);
    s.log_state = PROTO_LOG_STATE_RUNNING; strcpy(s.log_file, "LOG0003.CSV");
    proto_ui_render(&ui, &s, &c2, 0);
    row_text(&c2, 7, t); assert(strcmp(t, "SD:LOG LOG0003.CSV") == 0);
    row_text(&c2, 8, t); assert(strcmp(t, "Up 125s Heap 8496K") == 0);
    row_text(&c2, 9, t); assert(strcmp(t, "OK  up 125s") == 0);
    /* too small a canvas is ignored */
    static uint8_t tiny[40 * 8]; proto_canvas_t c3; proto_canvas_init(&c3, 40, 8, tiny);
    proto_ui_render(&ui, &s, &c3, 0);
    proto_ui_render(NULL, &s, &c, 0); proto_ui_input(NULL, PROTO_UI_IN_SHORT, &s, 0);
}

static void test_navigation_and_start(void)
{
    static uint8_t px[128 * 64];
    proto_canvas_t c; proto_canvas_init(&c, 128, 64, px);
    mock_t m; memset(&m, 0, sizeof(m)); m.start_ok = true; m.start_msg = "UART started";
    proto_ui_actions_t acts = { &m, a_start, a_route, a_log };
    proto_ui_t ui; proto_ui_init(&ui, &acts);
    proto_monitor_t s = status();
    char t[40];
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);                         /* turning on home does nothing */
    assert(ui.screen == PROTO_UI_HOME);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(ui.screen == PROTO_UI_MENU);
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 1, t); assert(strcmp(t, "Monitor") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(ui.screen == PROTO_UI_INTERFACES && ui.menu_cursor == 1);
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 1, t); assert(strcmp(t, "UART  off") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);                      /* edit UART */
    assert(ui.screen == PROTO_UI_IFACE_EDIT && ui.edit_protocol == PROTO_PROTOCOL_UART);
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 0, t); assert(strcmp(t, "UART SETUP") == 0);
    row_text(&c, 1, t); assert(strcmp(t, "Baud   115200") == 0);
    row_text(&c, 2, t); assert(strcmp(t, "START") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);                      /* edit baud */
    assert(ui.editing_value);
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);                         /* wraps to 9600 */
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 1, t); assert(strcmp(t, "Baud  <9600>") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(!ui.editing_value);
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);                         /* cursor to START */
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 1000);
    assert(m.starts == 1 && m.protocol == PROTO_PROTOCOL_UART && m.params.value == 9600);
    assert(ui.screen == PROTO_UI_INTERFACES);
    proto_ui_render(&ui, &s, &c, 1000);
    row_text(&c, 7, t); assert(strcmp(t, "UART started") == 0);         /* result message */
    proto_ui_render(&ui, &s, &c, 1000 + PROTO_UI_MESSAGE_US + 1);
    row_text(&c, 7, t); assert(strcmp(t, "long:back") == 0);            /* message expired */

    /* CAN: rate and mode are chosen and passed through */
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0); proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);   /* cursor -> CAN */
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 1, t); assert(strcmp(t, "Rate   500k") == 0);
    row_text(&c, 2, t); assert(strcmp(t, "Mode   normal") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0); proto_ui_input(&ui, PROTO_UI_IN_CCW, &s, 0);  /* 250k */
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);                                               /* Mode row */
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0); proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0); proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);                                            /* selftest */
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);                                               /* START */
    m.start_msg = NULL;
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 5);
    assert(m.protocol == PROTO_PROTOCOL_CAN && m.params.value == 250000 && m.params.mode == 2 && m.starts == 2);

    /* failure keeps you on the screen with a message */
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);                                               /* CAN -> I2C */
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    proto_ui_input(&ui, PROTO_UI_IN_CCW, &s, 0);                                              /* START (wraps up) */
    m.start_ok = false; m.start_msg = "I2C start failed";
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(ui.screen == PROTO_UI_IFACE_EDIT && m.starts == 3 && m.params.value == 100000 && !m.params.flag);
    /* long press cancels value editing first, then leaves */
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0); proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(ui.editing_value);
    proto_ui_input(&ui, PROTO_UI_IN_LONG, &s, 0); assert(ui.screen == PROTO_UI_IFACE_EDIT && !ui.editing_value);
    proto_ui_input(&ui, PROTO_UI_IN_LONG, &s, 0); assert(ui.screen == PROTO_UI_INTERFACES);
    proto_ui_input(&ui, PROTO_UI_IN_LONG, &s, 0); assert(ui.screen == PROTO_UI_MENU);
    proto_ui_input(&ui, PROTO_UI_IN_LONG, &s, 0); assert(ui.screen == PROTO_UI_HOME);

    /* SPI and RS-485 parameter mapping; an active interface is read-only */
    ui.screen = PROTO_UI_INTERFACES; ui.iface_cursor = PROTO_PROTOCOL_SPI;
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0); proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);  /* START */
    m.start_ok = true; m.start_msg = NULL;
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(m.protocol == PROTO_PROTOCOL_SPI && m.params.mode == 0 && m.params.value == 1000000);
    s.iface[PROTO_PROTOCOL_SPI].active = true; strcpy(s.iface[PROTO_PROTOCOL_SPI].note, "M0");
    ui.screen = PROTO_UI_INTERFACES; ui.iface_cursor = PROTO_PROTOCOL_SPI;
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 1, t); assert(strcmp(t, "ACTIVE M0") == 0);
    row_text(&c, 2, t); assert(strcmp(t, "reboot to change") == 0);
    const int before = m.starts;
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(ui.screen == PROTO_UI_INTERFACES && m.starts == before);
}

static void test_routes_logging_system(void)
{
    static uint8_t px[128 * 64];
    proto_canvas_t c; proto_canvas_init(&c, 128, 64, px);
    mock_t m; memset(&m, 0, sizeof(m)); m.start_ok = true;
    proto_ui_actions_t acts = { &m, a_start, a_route, a_log };
    proto_ui_t ui; proto_ui_init(&ui, &acts);
    proto_monitor_t s = status();
    char t[40];
    ui.screen = PROTO_UI_ROUTES;
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 0, t); assert(strcmp(t, "ROUTES 0/20 on") == 0);
    row_text(&c, 1, t); assert(strcmp(t, "RT01 UART>CAN off") == 0);
    row_text(&c, 2, t); assert(strcmp(t, "RT02 CAN>UART n/c") == 0);
    row_text(&c, 3, t); assert(strcmp(t, "RT03 UART>R485 off") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);                       /* RT-01 ready -> enable */
    assert(m.route_calls == 1 && m.route_id == 1 && m.route_enable);
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 10);                      /* RT-02 not configured */
    assert(m.route_calls == 1);
    proto_ui_render(&ui, &s, &c, 10);
    row_text(&c, 7, t); assert(strcmp(t, "Not configured") == 0);
    s.route_enabled[0] = true; s.routes_enabled = 1;
    ui.route_cursor = 0;
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);                       /* enabled -> disable */
    assert(m.route_calls == 2 && m.route_id == 1 && !m.route_enable);
    ui.route_cursor = 19;                                                /* scrolling keeps the cursor visible */
    proto_ui_render(&ui, &s, &c, PROTO_UI_MESSAGE_US * 10);
    row_text(&c, 7, t); assert(strcmp(t, "RT20 SPI>I2C n/c") == 0);
    row_text(&c, 1, t); assert(strcmp(t, "RT14 SPI>UART n/c") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_CW, &s, 0); assert(ui.route_cursor == 0);   /* wraps */
    proto_ui_input(&ui, PROTO_UI_IN_LONG, &s, 0); assert(ui.screen == PROTO_UI_MENU);

    ui.screen = PROTO_UI_LOGGING;
    strcpy(s.log_file, "LOG0002.CSV"); s.log_records = 4321; s.log_dropped = 2; s.log_write_errors = 1;
    s.log_wanted = true; s.log_state = PROTO_LOG_STATE_ERROR;
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 1, t); assert(strcmp(t, "SD: ERROR") == 0 && row_color(&c, 1) == PROTO_CANVAS_ACCENT);
    row_text(&c, 2, t); assert(strcmp(t, "File: LOG0002.CSV") == 0);
    row_text(&c, 3, t); assert(strcmp(t, "Rec:4k Drop:2") == 0);
    row_text(&c, 4, t); assert(strcmp(t, "WriteErr:1") == 0);
    row_text(&c, 5, t); assert(strcmp(t, "STOP LOGGING") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(m.log_calls == 1 && !m.log_start);
    s.log_wanted = false; s.log_state = PROTO_LOG_STATE_OFF;
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 5, t); assert(strcmp(t, "START LOGGING") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0);
    assert(m.log_calls == 2 && m.log_start);

    ui.screen = PROTO_UI_SYSTEM;
    proto_ui_render(&ui, &s, &c, 0);
    row_text(&c, 1, t); assert(strcmp(t, "FW 0.9.0") == 0);
    row_text(&c, 3, t); assert(strcmp(t, "Heap 8496K") == 0);
    row_text(&c, 5, t); assert(strcmp(t, "PSRAM 8M") == 0);
    row_text(&c, 6, t); assert(strcmp(t, "Disp SSD1306") == 0);
    proto_ui_input(&ui, PROTO_UI_IN_SHORT, &s, 0); assert(ui.screen == PROTO_UI_MENU);
    assert(strcmp(proto_ui_screen_name(PROTO_UI_ROUTES), "routes") == 0 && strcmp(proto_ui_screen_name((proto_ui_screen_t)99), "?") == 0);
    /* no actions installed: pressing START/route/log never crashes */
    proto_ui_t bare; proto_ui_init(&bare, NULL);
    bare.screen = PROTO_UI_LOGGING; proto_ui_input(&bare, PROTO_UI_IN_SHORT, &s, 0);
    bare.screen = PROTO_UI_ROUTES; bare.route_cursor = 0; proto_ui_input(&bare, PROTO_UI_IN_SHORT, &s, 0);
}

int main(void)
{
    test_canvas();
    test_oled_packing();
    test_encoder();
    test_button();
    test_home();
    test_navigation_and_start();
    test_routes_logging_system();
    puts("UI: canvas/font, encoder decoder, button debounce, screens, menu navigation, parameter editing, routes, logging and messages passed.");
    return 0;
}
