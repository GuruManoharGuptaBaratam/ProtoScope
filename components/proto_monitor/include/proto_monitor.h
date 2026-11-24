#pragma once

/* Communication monitor (Phase 8): one consistent snapshot of what ProtoScope is
 * doing, used by the console 'monitor' command and by the Phase 9 display. The
 * struct is plain data so the UI model is host-tested without hardware. */

#include <stdbool.h>
#include <stdint.h>

#include "proto_event.h"
#include "proto_logger.h"

#define PROTO_MONITOR_ROUTES 20

typedef struct {
    bool active;
    uint32_t rx;           /* frames / transfers received or completed reads */
    uint32_t tx;           /* frames / transfers transmitted or attempted */
    uint32_t errors;       /* all error counters summed */
    uint32_t drops;        /* explicit queue drops / rejected requests */
    bool warning;          /* e.g. CAN error-passive or bus-off */
    char note[10];         /* short setting text: baud, bit rate, mode */
} proto_monitor_iface_t;

typedef struct {
    char firmware[12];
    uint32_t uptime_s;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t psram_bytes;
    proto_monitor_iface_t iface[PROTO_PROTOCOL_COUNT];

    uint8_t routes_enabled;
    uint8_t routes_ready;
    uint8_t route_src[PROTO_MONITOR_ROUTES];
    uint8_t route_dst[PROTO_MONITOR_ROUTES];
    bool route_enabled[PROTO_MONITOR_ROUTES];
    bool route_ready[PROTO_MONITOR_ROUTES];
    uint32_t route_transmitted;
    uint32_t route_rejected;

    proto_log_state_t log_state;
    bool log_wanted;
    bool card_mounted;
    char log_file[PROTO_LOG_NAME_MAX];
    uint32_t log_records;
    uint32_t log_dropped;
    uint32_t log_write_errors;

    char display_name[12];
    bool display_active;
} proto_monitor_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Fills everything except display_name/display_active (set by the UI runtime). */
void proto_monitor_collect(proto_monitor_t *out);

#ifdef __cplusplus
}
#endif
