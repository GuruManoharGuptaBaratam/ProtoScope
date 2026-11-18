#pragma once

#include "esp_err.h"

/* Starts the routing runtime and registers the 'route' command. */
esp_err_t proto_route_console_register(void);
void proto_route_console_print_status(void);
