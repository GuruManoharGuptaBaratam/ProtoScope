#include "proto_monitor_console.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_console.h"
#include "proto_event_caps.h"
#include "proto_monitor.h"

static int monitor_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static proto_monitor_t m;   /* ~1 KB: keep it off the console task stack */
    proto_monitor_collect(&m);
    printf("monitor_system firmware=%s uptime_s=%" PRIu32 " free_heap=%" PRIu32 " min_free_heap=%" PRIu32
           " psram_bytes=%" PRIu32 "\n", m.firmware, m.uptime_s, m.free_heap, m.min_free_heap, m.psram_bytes);
    for (int p = 0; p < PROTO_PROTOCOL_COUNT; ++p) {
        const proto_monitor_iface_t *i = &m.iface[p];
        printf("monitor_if proto=%s active=%s rx=%" PRIu32 " tx=%" PRIu32 " errors=%" PRIu32
               " drops=%" PRIu32 " warning=%s note=%s\n", proto_protocol_name((proto_protocol_t)p),
               i->active ? "yes" : "no", i->rx, i->tx, i->errors, i->drops, i->warning ? "yes" : "no",
               i->note[0] ? i->note : "-");
    }
    printf("monitor_routes defined=%u enabled=%u ready=%u transmitted=%" PRIu32 " rejected=%" PRIu32 "\n",
           (unsigned)PROTO_MONITOR_ROUTES, (unsigned)m.routes_enabled, (unsigned)m.routes_ready,
           m.route_transmitted, m.route_rejected);
    printf("monitor_log state=%s card=%s file=%s records=%" PRIu32 " dropped=%" PRIu32 " write_errors=%" PRIu32 "\n",
           proto_logger_state_name(m.log_state), m.card_mounted ? "mounted" : "none",
           m.log_file[0] ? m.log_file : "-", m.log_records, m.log_dropped, m.log_write_errors);
    puts("monitor_end");
    return 0;
}

esp_err_t proto_monitor_console_register(void)
{
    const esp_console_cmd_t command = {
        .command = "monitor",
        .help = "Phase 8 monitor: interfaces, routes, storage and system in one snapshot",
        .func = monitor_command,
    };
    return esp_console_cmd_register(&command);
}
