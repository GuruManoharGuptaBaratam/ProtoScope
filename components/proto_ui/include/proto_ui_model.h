#pragma once

/* Menu / screen model for the local display and rotary encoder (Phase 9). Pure C:
 * the model turns encoder events and a monitor snapshot into text rows, and calls
 * action callbacks for the few things the UI can really do. It lists only features
 * that exist: start an interface, enable or disable a ready route, start or stop
 * logging, show status. */

#include <stdbool.h>
#include <stdint.h>

#include "proto_canvas.h"
#include "proto_monitor.h"

typedef enum {
    PROTO_UI_IN_NONE = 0,
    PROTO_UI_IN_CW,
    PROTO_UI_IN_CCW,
    PROTO_UI_IN_SHORT,
    PROTO_UI_IN_LONG,
} proto_ui_input_t;

typedef enum {
    PROTO_UI_HOME = 0,
    PROTO_UI_MENU,
    PROTO_UI_INTERFACES,
    PROTO_UI_IFACE_EDIT,
    PROTO_UI_ROUTES,
    PROTO_UI_LOGGING,
    PROTO_UI_SYSTEM,
} proto_ui_screen_t;

/* Interface start parameters chosen on screen (numeric, meaning per protocol):
 * UART  value=baud (9600|115200)             RS485 value=baud, flag=keep echo
 * CAN   value=bit rate, mode 0 normal 1 listen 2 selftest
 * I2C   value=frequency Hz, flag=internal pull-up
 * SPI   value=clock Hz, mode 0..3                                          */
typedef struct {
    uint32_t value;
    uint8_t mode;
    bool flag;
} proto_ui_params_t;

typedef struct {
    void *context;
    /* Return true on success; *message is a short (<= 21 char) result text. */
    bool (*start_interface)(void *context, proto_protocol_t protocol, const proto_ui_params_t *params, const char **message);
    bool (*route_toggle)(void *context, uint8_t route_id, bool enable, const char **message);
    bool (*log_toggle)(void *context, bool start, const char **message);
} proto_ui_actions_t;

#define PROTO_UI_MESSAGE_US 2500000ull
#define PROTO_UI_MENU_ITEMS 5
#define PROTO_UI_MAX_PARAM_ROWS 3

typedef struct {
    proto_ui_actions_t actions;
    proto_ui_screen_t screen;
    uint8_t menu_cursor;
    uint8_t iface_cursor;            /* 0..4 = protocol index */
    proto_protocol_t edit_protocol;
    uint8_t edit_cursor;             /* parameter rows then the Start row */
    bool editing_value;              /* encoder turns the selected value */
    uint8_t value_index[PROTO_UI_MAX_PARAM_ROWS];
    uint8_t route_cursor;            /* 0..19 */
    char message[24];
    uint64_t message_until_us;
    uint32_t input_count;
} proto_ui_t;

void proto_ui_init(proto_ui_t *ui, const proto_ui_actions_t *actions);
void proto_ui_input(proto_ui_t *ui, proto_ui_input_t input, const proto_monitor_t *status, uint64_t now_us);
void proto_ui_render(proto_ui_t *ui, const proto_monitor_t *status, proto_canvas_t *canvas, uint64_t now_us);
const char *proto_ui_screen_name(proto_ui_screen_t screen);
