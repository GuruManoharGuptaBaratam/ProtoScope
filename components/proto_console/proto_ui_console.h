#pragma once

#include "esp_err.h"

/* Starts the UI runtime and registers the 'display' and 'ui' commands. */
esp_err_t proto_ui_console_register(void);
void proto_ui_console_print_status(void);
