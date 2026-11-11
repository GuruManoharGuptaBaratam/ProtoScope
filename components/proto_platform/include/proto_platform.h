#pragma once

#include <stdio.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t proto_platform_init(void);
void proto_platform_log_boot_summary(void);
void proto_platform_print_status(FILE *stream);

#ifdef __cplusplus
}
#endif
