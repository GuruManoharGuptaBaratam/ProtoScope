#include "proto_ui_model.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "proto_event_caps.h"

static const char *const k_short[PROTO_PROTOCOL_COUNT] = { "UART", "R485", "CAN", "I2C", "SPI" };

typedef struct {
    const char *label;
    uint8_t count;
    const char *text[5];
    uint32_t number[5];
    uint8_t default_index;
} param_def_t;

typedef struct {
    uint8_t count;
    param_def_t param[PROTO_UI_MAX_PARAM_ROWS];
} iface_def_t;

static const iface_def_t k_iface[PROTO_PROTOCOL_COUNT] = {
    [PROTO_PROTOCOL_UART] = { 1, { { "Baud", 2, { "9600", "115200" }, { 9600, 115200 }, 1 } } },
    [PROTO_PROTOCOL_RS485] = { 2, { { "Baud", 5, { "9600", "19200", "38400", "57600", "115200" },
                                      { 9600, 19200, 38400, 57600, 115200 }, 0 },
                                    { "Echo", 2, { "discard", "keep" }, { 0, 1 }, 0 } } },
    [PROTO_PROTOCOL_CAN] = { 2, { { "Rate", 4, { "125k", "250k", "500k", "1M" }, { 125000, 250000, 500000, 1000000 }, 2 },
                                  { "Mode", 3, { "normal", "listen", "selftest" }, { 0, 1, 2 }, 0 } } },
    [PROTO_PROTOCOL_I2C] = { 2, { { "Freq", 2, { "100k", "400k" }, { 100000, 400000 }, 0 },
                                  { "Pullup", 2, { "off", "on" }, { 0, 1 }, 0 } } },
    [PROTO_PROTOCOL_SPI] = { 2, { { "Mode", 4, { "0", "1", "2", "3" }, { 0, 1, 2, 3 }, 0 },
                                  { "Clock", 5, { "100k", "500k", "1M", "4M", "8M" },
                                    { 100000, 500000, 1000000, 4000000, 8000000 }, 2 } } },
};

static const char *const k_menu[PROTO_UI_MENU_ITEMS] = { "Monitor", "Interfaces", "Routes", "Logging", "System" };

const char *proto_ui_screen_name(proto_ui_screen_t screen)
{
    static const char *const names[] = { "home", "menu", "interfaces", "iface_edit", "routes", "logging", "system" };
    return (unsigned)screen <= PROTO_UI_SYSTEM ? names[screen] : "?";
}

void proto_ui_init(proto_ui_t *ui, const proto_ui_actions_t *actions)
{
    if (!ui) { return; }
    memset(ui, 0, sizeof(*ui));
    if (actions) { ui->actions = *actions; }
    ui->screen = PROTO_UI_HOME;
}

static void show_message(proto_ui_t *ui, const char *text, uint64_t now_us)
{
    snprintf(ui->message, sizeof(ui->message), "%s", text ? text : "");
    ui->message_until_us = now_us + PROTO_UI_MESSAGE_US;
}

static void reset_edit_values(proto_ui_t *ui, proto_protocol_t p)
{
    for (uint8_t i = 0; i < PROTO_UI_MAX_PARAM_ROWS; ++i) {
        ui->value_index[i] = i < k_iface[p].count ? k_iface[p].param[i].default_index : 0;
    }
}

static proto_ui_params_t params_for(const proto_ui_t *ui, proto_protocol_t p)
{
    proto_ui_params_t out;
    memset(&out, 0, sizeof(out));
    const iface_def_t *d = &k_iface[p];
    const uint32_t v0 = d->param[0].number[ui->value_index[0]];
    const uint32_t v1 = d->count > 1 ? d->param[1].number[ui->value_index[1]] : 0;
    switch (p) {
    case PROTO_PROTOCOL_UART: out.value = v0; break;
    case PROTO_PROTOCOL_RS485: out.value = v0; out.flag = v1 != 0; break;
    case PROTO_PROTOCOL_CAN: out.value = v0; out.mode = (uint8_t)v1; break;
    case PROTO_PROTOCOL_I2C: out.value = v0; out.flag = v1 != 0; break;
    case PROTO_PROTOCOL_SPI: out.mode = (uint8_t)v0; out.value = v1; break;
    case PROTO_PROTOCOL_COUNT: break;
    }
    return out;
}

static uint8_t wrap(int value, int count)
{
    return (uint8_t)(((value % count) + count) % count);
}

static void move_cursor(uint8_t *cursor, proto_ui_input_t input, int count)
{
    if (input == PROTO_UI_IN_CW) { *cursor = wrap(*cursor + 1, count); }
    if (input == PROTO_UI_IN_CCW) { *cursor = wrap(*cursor - 1, count); }
}

void proto_ui_input(proto_ui_t *ui, proto_ui_input_t input, const proto_monitor_t *st, uint64_t now_us)
{
    if (!ui || !st || input == PROTO_UI_IN_NONE) { return; }
    ui->input_count++;
    const char *message = NULL;
    switch (ui->screen) {
    case PROTO_UI_HOME:
        if (input == PROTO_UI_IN_SHORT) { ui->screen = PROTO_UI_MENU; }
        break;
    case PROTO_UI_MENU:
        move_cursor(&ui->menu_cursor, input, PROTO_UI_MENU_ITEMS);
        if (input == PROTO_UI_IN_LONG) { ui->screen = PROTO_UI_HOME; }
        if (input == PROTO_UI_IN_SHORT) {
            static const proto_ui_screen_t target[PROTO_UI_MENU_ITEMS] = {
                PROTO_UI_HOME, PROTO_UI_INTERFACES, PROTO_UI_ROUTES, PROTO_UI_LOGGING, PROTO_UI_SYSTEM };
            ui->screen = target[ui->menu_cursor];
        }
        break;
    case PROTO_UI_INTERFACES:
        move_cursor(&ui->iface_cursor, input, PROTO_PROTOCOL_COUNT);
        if (input == PROTO_UI_IN_LONG) { ui->screen = PROTO_UI_MENU; }
        if (input == PROTO_UI_IN_SHORT) {
            ui->edit_protocol = (proto_protocol_t)ui->iface_cursor;
            ui->edit_cursor = 0;
            ui->editing_value = false;
            reset_edit_values(ui, ui->edit_protocol);
            ui->screen = PROTO_UI_IFACE_EDIT;
        }
        break;
    case PROTO_UI_IFACE_EDIT: {
        const iface_def_t *d = &k_iface[ui->edit_protocol];
        const bool active = st->iface[ui->edit_protocol].active;
        if (active) {                                   /* read-only once started */
            if (input == PROTO_UI_IN_SHORT || input == PROTO_UI_IN_LONG) { ui->screen = PROTO_UI_INTERFACES; }
            break;
        }
        const int rows = d->count + 1;                  /* parameters + START */
        if (input == PROTO_UI_IN_LONG) {
            if (ui->editing_value) { ui->editing_value = false; }
            else { ui->screen = PROTO_UI_INTERFACES; }
            break;
        }
        if (ui->editing_value && (input == PROTO_UI_IN_CW || input == PROTO_UI_IN_CCW)) {
            const param_def_t *p = &d->param[ui->edit_cursor];
            ui->value_index[ui->edit_cursor] = wrap(ui->value_index[ui->edit_cursor] + (input == PROTO_UI_IN_CW ? 1 : -1), p->count);
            break;
        }
        if (!ui->editing_value) { move_cursor(&ui->edit_cursor, input, rows); }
        if (input == PROTO_UI_IN_SHORT) {
            if (ui->edit_cursor < d->count) {
                ui->editing_value = !ui->editing_value;
            } else if (ui->actions.start_interface) {
                const proto_ui_params_t params = params_for(ui, ui->edit_protocol);
                const bool ok = ui->actions.start_interface(ui->actions.context, ui->edit_protocol, &params, &message);
                show_message(ui, message ? message : (ok ? "Started" : "Start failed"), now_us);
                if (ok) { ui->screen = PROTO_UI_INTERFACES; }
            }
        }
        break;
    }
    case PROTO_UI_ROUTES:
        move_cursor(&ui->route_cursor, input, PROTO_MONITOR_ROUTES);
        if (input == PROTO_UI_IN_LONG) { ui->screen = PROTO_UI_MENU; }
        if (input == PROTO_UI_IN_SHORT) {
            const uint8_t i = ui->route_cursor;
            if (st->route_enabled[i]) {
                const bool ok = ui->actions.route_toggle &&
                    ui->actions.route_toggle(ui->actions.context, (uint8_t)(i + 1), false, &message);
                show_message(ui, message ? message : (ok ? "Route disabled" : "Failed"), now_us);
            } else if (st->route_ready[i]) {
                const bool ok = ui->actions.route_toggle &&
                    ui->actions.route_toggle(ui->actions.context, (uint8_t)(i + 1), true, &message);
                show_message(ui, message ? message : (ok ? "Route enabled" : "Failed"), now_us);
            } else {
                show_message(ui, "Not configured", now_us);
            }
        }
        break;
    case PROTO_UI_LOGGING:
        if (input == PROTO_UI_IN_LONG) { ui->screen = PROTO_UI_MENU; }
        if (input == PROTO_UI_IN_SHORT) {
            const bool start = !st->log_wanted;
            const bool ok = ui->actions.log_toggle && ui->actions.log_toggle(ui->actions.context, start, &message);
            show_message(ui, message ? message : (ok ? (start ? "Logging" : "Stopped") : "Failed"), now_us);
        }
        break;
    case PROTO_UI_SYSTEM:
        if (input == PROTO_UI_IN_SHORT || input == PROTO_UI_IN_LONG) { ui->screen = PROTO_UI_MENU; }
        break;
    }
}

/* ---- rendering -------------------------------------------------------------------------- */
static void fmt_count(char *out, size_t size, uint32_t v)
{
    if (v < 1000) { snprintf(out, size, "%u", (unsigned)v); }
    else if (v < 1000000) { snprintf(out, size, "%uk", (unsigned)(v / 1000)); }
    else { snprintf(out, size, "%uM", (unsigned)(v / 1000000)); }
}

static const char *log_short(proto_log_state_t s)
{
    switch (s) {
    case PROTO_LOG_STATE_RUNNING: return "LOG";
    case PROTO_LOG_STATE_NO_CARD: return "NO CARD";
    case PROTO_LOG_STATE_FULL: return "FULL";
    case PROTO_LOG_STATE_ERROR: return "ERROR";
    default: return "off";
    }
}

/* One-line warning about the most important problem, or NULL. */
static const char *warning_text(const proto_monitor_t *st, char *buf, size_t size)
{
    if (st->log_wanted && st->log_state != PROTO_LOG_STATE_RUNNING) {
        snprintf(buf, size, "!SD %s", log_short(st->log_state));
        return buf;
    }
    for (int p = 0; p < PROTO_PROTOCOL_COUNT; ++p) {
        if (st->iface[p].active && st->iface[p].warning) {
            snprintf(buf, size, "!%s ERRORS", k_short[p]);
            return buf;
        }
    }
    return NULL;
}

static void row(proto_canvas_t *c, uint16_t r, bool inverted, bool accent, const char *fmt, ...) __attribute__((format(printf, 5, 6)));

static void row(proto_canvas_t *c, uint16_t r, bool inverted, bool accent, const char *fmt, ...)
{
    char line[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    proto_canvas_text_row(c, r, line, inverted, accent);
}

static void render_home(const proto_monitor_t *st, proto_canvas_t *c, uint16_t rows)
{
    row(c, 0, true, false, "PROTOSCOPE %s", st->firmware);
    for (int p = 0; p < PROTO_PROTOCOL_COUNT; ++p) {
        const proto_monitor_iface_t *i = &st->iface[p];
        char rx[8], tx[8], er[8];
        fmt_count(rx, sizeof(rx), i->rx); fmt_count(tx, sizeof(tx), i->tx); fmt_count(er, sizeof(er), i->errors);
        if (!i->active) {
            row(c, (uint16_t)(1 + p), false, false, "%-4s- off", k_short[p]);
        } else {
            row(c, (uint16_t)(1 + p), false, i->warning || i->errors > 0, "%-4s%c %s/%s E%s", k_short[p],
                i->warning ? '!' : '*', rx, tx, er);
        }
    }
    if (rows >= 10) {
        row(c, 6, false, false, "RT:%u/%u on  tx%u", (unsigned)st->routes_enabled, (unsigned)PROTO_MONITOR_ROUTES,
            (unsigned)st->route_transmitted);
        row(c, 7, false, false, "SD:%s %s", log_short(st->log_state), st->log_file[0] ? st->log_file : "");
        row(c, 8, false, false, "Up %us Heap %uK", (unsigned)st->uptime_s, (unsigned)(st->free_heap / 1024));
    } else {
        row(c, 6, false, false, "RT:%u/%u SD:%s", (unsigned)st->routes_enabled, (unsigned)PROTO_MONITOR_ROUTES,
            log_short(st->log_state));
    }
    char buf[24];
    const char *warn = warning_text(st, buf, sizeof(buf));
    if (warn) { row(c, (uint16_t)(rows - 1), true, true, "%s", warn); }   /* highlighted bar: visible on a mono OLED */
    else { row(c, (uint16_t)(rows - 1), false, false, "OK  up %us", (unsigned)st->uptime_s); }
}

static void render_menu(const proto_ui_t *ui, proto_canvas_t *c, uint16_t rows)
{
    row(c, 0, true, false, "MENU");
    for (int i = 0; i < PROTO_UI_MENU_ITEMS; ++i) {
        row(c, (uint16_t)(1 + i), i == ui->menu_cursor, false, "%s", k_menu[i]);
    }
    row(c, (uint16_t)(rows - 1), false, false, "turn:move press:ok");
}

static void render_interfaces(const proto_ui_t *ui, const proto_monitor_t *st, proto_canvas_t *c, uint16_t rows)
{
    row(c, 0, true, false, "INTERFACES");
    for (int p = 0; p < PROTO_PROTOCOL_COUNT; ++p) {
        const proto_monitor_iface_t *i = &st->iface[p];
        row(c, (uint16_t)(1 + p), p == ui->iface_cursor, false, "%-5s %s %s", proto_protocol_name((proto_protocol_t)p),
            i->active ? "ON" : "off", i->active ? i->note : "");
    }
    row(c, (uint16_t)(rows - 1), false, false, "long:back");
}

static void render_edit(const proto_ui_t *ui, const proto_monitor_t *st, proto_canvas_t *c, uint16_t rows)
{
    const proto_protocol_t p = ui->edit_protocol;
    const iface_def_t *d = &k_iface[p];
    row(c, 0, true, false, "%s SETUP", proto_protocol_name(p));
    if (st->iface[p].active) {
        row(c, 1, false, false, "ACTIVE %s", st->iface[p].note);
        row(c, 2, false, false, "reboot to change");
        row(c, (uint16_t)(rows - 1), false, false, "press:back");
        return;
    }
    for (uint8_t i = 0; i < d->count; ++i) {
        const param_def_t *pd = &d->param[i];
        const bool sel = ui->edit_cursor == i;
        row(c, (uint16_t)(1 + i), sel && !ui->editing_value, false, "%-6s%s%s%s", pd->label,
            sel && ui->editing_value ? "<" : " ", pd->text[ui->value_index[i]], sel && ui->editing_value ? ">" : "");
    }
    row(c, (uint16_t)(1 + d->count), ui->edit_cursor == d->count, false, "START");
    row(c, (uint16_t)(rows - 1), false, false, ui->editing_value ? "turn:value press:ok" : "press:select");
}

static void render_routes(const proto_ui_t *ui, const proto_monitor_t *st, proto_canvas_t *c, uint16_t rows)
{
    row(c, 0, true, false, "ROUTES %u/%u on", (unsigned)st->routes_enabled, (unsigned)PROTO_MONITOR_ROUTES);
    const uint16_t visible = (uint16_t)(rows - 1);
    const uint16_t first = ui->route_cursor >= visible ? (uint16_t)(ui->route_cursor - visible + 1) : 0;
    for (uint16_t r = 0; r < visible && (first + r) < PROTO_MONITOR_ROUTES; ++r) {
        const uint16_t i = (uint16_t)(first + r);
        row(c, (uint16_t)(1 + r), i == ui->route_cursor, false, "RT%02u %s>%s %s", (unsigned)(i + 1),
            k_short[st->route_src[i] < PROTO_PROTOCOL_COUNT ? st->route_src[i] : 0],
            k_short[st->route_dst[i] < PROTO_PROTOCOL_COUNT ? st->route_dst[i] : 0],
            st->route_enabled[i] ? "ON" : st->route_ready[i] ? "off" : "n/c");
    }
}

static void render_logging(const proto_monitor_t *st, proto_canvas_t *c, uint16_t rows)
{
    char rec[8], drop[8];
    fmt_count(rec, sizeof(rec), st->log_records);
    fmt_count(drop, sizeof(drop), st->log_dropped);
    row(c, 0, true, false, "LOGGING");
    row(c, 1, false, st->log_wanted && st->log_state != PROTO_LOG_STATE_RUNNING, "SD: %s", log_short(st->log_state));
    row(c, 2, false, false, "File: %s", st->log_file[0] ? st->log_file : "-");
    row(c, 3, false, false, "Rec:%s Drop:%s", rec, drop);
    row(c, 4, false, st->log_write_errors > 0, "WriteErr:%u", (unsigned)st->log_write_errors);
    row(c, 5, true, false, "%s", st->log_wanted ? "STOP LOGGING" : "START LOGGING");
    row(c, (uint16_t)(rows - 1), false, false, "long:back");
}

static void render_system(const proto_monitor_t *st, proto_canvas_t *c, uint16_t rows)
{
    row(c, 0, true, false, "SYSTEM");
    row(c, 1, false, false, "FW %s", st->firmware);
    row(c, 2, false, false, "Up %us", (unsigned)st->uptime_s);
    row(c, 3, false, false, "Heap %uK", (unsigned)(st->free_heap / 1024));
    row(c, 4, false, false, "MinHeap %uK", (unsigned)(st->min_free_heap / 1024));
    row(c, 5, false, false, "PSRAM %uM", (unsigned)(st->psram_bytes / (1024 * 1024)));
    row(c, 6, false, false, "Disp %s%s", st->display_name, st->display_active ? "" : " (off)");
    row(c, (uint16_t)(rows - 1), false, false, "press:back");
}

void proto_ui_render(proto_ui_t *ui, const proto_monitor_t *st, proto_canvas_t *c, uint64_t now_us)
{
    if (!ui || !st || !c) { return; }
    const uint16_t rows = proto_canvas_rows(c);
    if (rows < 8) { return; }
    proto_canvas_clear(c, PROTO_CANVAS_BG);
    switch (ui->screen) {
    case PROTO_UI_HOME: render_home(st, c, rows); break;
    case PROTO_UI_MENU: render_menu(ui, c, rows); break;
    case PROTO_UI_INTERFACES: render_interfaces(ui, st, c, rows); break;
    case PROTO_UI_IFACE_EDIT: render_edit(ui, st, c, rows); break;
    case PROTO_UI_ROUTES: render_routes(ui, st, c, rows); break;
    case PROTO_UI_LOGGING: render_logging(st, c, rows); break;
    case PROTO_UI_SYSTEM: render_system(st, c, rows); break;
    }
    if (ui->message[0] && now_us < ui->message_until_us) {
        row(c, (uint16_t)(rows - 1), true, true, "%s", ui->message);
    }
}
