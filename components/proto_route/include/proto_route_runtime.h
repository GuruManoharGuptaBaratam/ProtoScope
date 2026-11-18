#pragma once

/* Binds the host-tested routing engine to the real interface drivers
 * (through the Phase 6 common driver contract) and runs it in one task. */

#include "esp_err.h"
#include "proto_route.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the engine (all 20 routes disabled) and its task. Idempotent. The task
 * only touches interfaces that are started AND used by an enabled route. */
esp_err_t proto_route_runtime_start(void);
/* The engine is shared with the task: hold the lock while reading or changing it. */
void proto_route_runtime_lock(void);
void proto_route_runtime_unlock(void);
proto_route_engine_t *proto_route_runtime_engine(void);

#ifdef __cplusplus
}
#endif
