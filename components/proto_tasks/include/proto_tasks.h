#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t supervisor_cycles;
    uint32_t worker_messages;
    uint32_t queue_overflows;
} proto_tasks_snapshot_t;

esp_err_t proto_tasks_start(void);
proto_tasks_snapshot_t proto_tasks_get_snapshot(void);

#ifdef __cplusplus
}
#endif
