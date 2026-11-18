#include "proto_route_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_hex.h"
#include "proto_route.h"
#include "proto_route_runtime.h"

static bool parse_id(const char *text, uint8_t *id)
{
    if (strncmp(text, "RT-", 3) == 0 || strncmp(text, "rt-", 3) == 0) { text += 3; }
    char *end = NULL;
    const unsigned long v = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || v < 1 || v > PROTO_ROUTE_COUNT) { return false; }
    *id = (uint8_t)v;
    return true;
}

void proto_route_console_print_status(void)
{
    proto_route_runtime_lock();
    const proto_route_engine_t *e = proto_route_runtime_engine();
    unsigned enabled = 0;
    for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) { enabled += e->routes[i].config.enabled; }
    printf("routes_defined=%u routes_enabled=%u engine_steps=%" PRIu32 " loop_window_ms=%" PRIu32 "\n",
           (unsigned)PROTO_ROUTE_COUNT, enabled, e->steps, e->loop_window_ms);
    proto_route_runtime_unlock();
}

static void print_stats_line(const proto_route_slot_t *slot)
{
    char name[8];
    proto_route_name(slot->config.id, name, sizeof(name));
    const proto_route_stats_t *s = &slot->stats;
    printf("route_stats id=%s enabled=%s matched=%" PRIu32 " filtered=%" PRIu32 " transmitted=%" PRIu32
           " rejected_decode=%" PRIu32 " rejected_map=%" PRIu32 " rejected_encode=%" PRIu32
           " dest_unavailable=%" PRIu32 " dest_failures=%" PRIu32 " dropped_busy=%" PRIu32
           " timed_out=%" PRIu32 " polls=%" PRIu32 " poll_failures=%" PRIu32 " pending=%s"
           " last_error=%d last_reject=%s\n", name, slot->config.enabled ? "yes" : "no",
           s->matched, s->filtered, s->transmitted, s->rejected_decode, s->rejected_map,
           s->rejected_encode, s->dest_unavailable, s->dest_failures, s->dropped_busy, s->timed_out,
           s->polls, s->poll_failures, slot->pending ? "yes" : "no", (int)s->last_error,
           proto_proc_result_name(s->last_reject));
}

static void print_show(const proto_route_t *r)
{
    char line[320], hex[2 * PROTO_ROUTE_PREFIX_MAX + 1];
    (void)proto_route_format(r, line, sizeof(line));
    puts(line);
    if (r->has_filter) {
        printf("  filter=%" PRIX32 "/%" PRIX32 " filter_ext=%d\n", r->filter_id, r->filter_mask, r->filter_extended);
    }
    if (r->poll_configured) {
        if (r->source == PROTO_PROTOCOL_I2C) {
            char w[2 * PROTO_I2C_MAX_TRANSFER + 1];
            (void)proto_hex_format(r->poll_i2c.write_data, r->poll_i2c.write_length, w, sizeof(w));
            printf("  poll_i2c addr=0x%02X write=%s read_len=%u interval_ms=%" PRIu32 "\n",
                   r->poll_i2c.address, w, (unsigned)r->poll_i2c.read_length, r->poll_interval_ms);
        } else {
            char w[2 * PROTO_SPI_MAX_TRANSFER + 1];
            (void)proto_hex_format(r->poll_spi, r->poll_spi_length, w, sizeof(w));
            printf("  poll_spi tx=%s interval_ms=%" PRIu32 "\n", w, r->poll_interval_ms);
        }
    }
    if (r->mapper == PROTO_MAP_SLICE) {
        printf("  slice offset=%u length=%u\n", (unsigned)r->slice_offset, (unsigned)r->slice_length);
    } else if (r->mapper == PROTO_MAP_SCALE) {
        printf("  scale in=%u/%s/%s mul=%d div=%d add=%d out=%u/%s/%s\n", (unsigned)r->scale.in_width,
               r->scale.in_big_endian ? "be" : "le", r->scale.in_signed ? "s" : "u", (int)r->scale.mul,
               (int)r->scale.div, (int)r->scale.add, (unsigned)r->scale.out_width,
               r->scale.out_big_endian ? "be" : "le", r->scale.out_signed ? "s" : "u");
    }
    if (r->destination == PROTO_PROTOCOL_CAN) {
        if (r->can_id_from_datum) { puts("  can_id=datum"); }
        else { printf("  can_id=%" PRIX32 " can_ext=%d set=%d\n", r->can_id, r->can_extended, r->can_id_set); }
    }
    if (r->destination == PROTO_PROTOCOL_I2C) { printf("  i2c_addr=0x%02X\n", r->i2c_address); }
    if (r->destination == PROTO_PROTOCOL_I2C || r->destination == PROTO_PROTOCOL_SPI) {
        (void)proto_hex_format(r->prefix, r->prefix_length, hex, sizeof(hex));
        printf("  prefix=%s prefix_set=%d\n", r->prefix_length ? hex : "none", r->prefix_set);
    }
    puts("route_show_end");
}

static int route_command(int argc, char **argv)
{
    uint8_t id = 0;
    int result = 0;
    proto_route_runtime_lock();
    proto_route_engine_t *e = proto_route_runtime_engine();
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (uint8_t i = 1; i <= PROTO_ROUTE_COUNT; ++i) {
            char line[320];
            (void)proto_route_format(proto_route_engine_route(e, i), line, sizeof(line));
            puts(line);
        }
        puts("route_list_end");
    } else if (argc == 3 && strcmp(argv[1], "show") == 0 && parse_id(argv[2], &id)) {
        print_show(proto_route_engine_route(e, id));
    } else if (argc >= 4 && strcmp(argv[1], "set") == 0 && parse_id(argv[2], &id)) {
        proto_route_t trial = *proto_route_engine_route(e, id);
        bool ok = true;
        for (int i = 3; i < argc && ok; ++i) {
            char *eq = strchr(argv[i], '=');
            if (!eq) { ok = false; printf("route_error=bad_argument arg=%s\n", argv[i]); break; }
            *eq = '\0';
            const proto_route_set_result_t r = proto_route_set(&trial, argv[i], eq + 1);
            if (r != PROTO_SET_OK) {
                ok = false;
                printf("route_error=%s key=%s\n", r == PROTO_SET_UNKNOWN_KEY ? "unknown_key" : "bad_value", argv[i]);
            }
        }
        if (ok) {
            /* Never change a running route into an invalid one. */
            if (trial.enabled && proto_route_validate(&trial) != PROTO_ROUTE_OK) {
                printf("route_error=%s (disable the route first)\n", proto_route_check_name(proto_route_validate(&trial)));
                ok = false;
            } else {
                *proto_route_engine_route(e, id) = trial;
                printf("route_set=ok config=%s\n", proto_route_check_name(proto_route_validate(&trial)));
            }
        }
        result = ok ? 0 : 1;
    } else if (argc == 3 && strcmp(argv[1], "enable") == 0 && parse_id(argv[2], &id)) {
        const proto_route_check_t c = proto_route_engine_enable(e, id);
        if (c == PROTO_ROUTE_OK) { printf("route_enable=ok\n"); }
        else { printf("route_error=%s\n", proto_route_check_name(c)); result = 1; }
    } else if (argc == 3 && strcmp(argv[1], "disable") == 0 && parse_id(argv[2], &id)) {
        proto_route_engine_disable(e, id);
        puts("route_disable=ok");
    } else if (argc == 3 && strcmp(argv[1], "trigger") == 0 && parse_id(argv[2], &id)) {
        if (proto_route_engine_trigger(e, id)) { puts("route_trigger=queued"); }
        else { puts("route_error=not_a_polled_source_or_disabled"); result = 1; }
    } else if (argc >= 2 && argc <= 3 && strcmp(argv[1], "stats") == 0) {
        const bool all = argc == 3 && strcmp(argv[2], "all") == 0;
        if (argc == 3 && !all && parse_id(argv[2], &id)) {
            print_stats_line(&e->routes[id - 1]);
        } else if (argc == 2 || all) {
            for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) {
                const proto_route_stats_t *s = &e->routes[i].stats;
                if (all || e->routes[i].config.enabled || s->matched || s->polls || s->transmitted) {
                    print_stats_line(&e->routes[i]);
                }
            }
        } else {
            puts("route_error=bad_argument");
            result = 1;
        }
        for (int p = 0; p < PROTO_PROTOCOL_COUNT; ++p) {
            printf("route_iface proto=%s rx_events=%" PRIu32 " loop_suppressed=%" PRIu32
                   " loop_rejected=%" PRIu32 " tx_submitted=%" PRIu32 "\n",
                   proto_protocol_name((proto_protocol_t)p), e->iface[p].rx_events,
                   e->iface[p].loop_suppressed, e->iface[p].loop_rejected, e->iface[p].tx_submitted);
        }
        printf("route_engine steps=%" PRIu32 " loop_window_ms=%" PRIu32 "\n", e->steps, e->loop_window_ms);
        puts("route_stats_end");
    } else if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        proto_route_engine_reset_stats(e);
        puts("route_reset=ok");
    } else if (argc == 3 && strcmp(argv[1], "loopwindow") == 0) {
        char *end = NULL;
        const unsigned long ms = strtoul(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || ms > 5000) { puts("route_error=bad_value"); result = 1; }
        else { e->loop_window_ms = (uint32_t)ms; printf("route_loopwindow=ok ms=%lu\n", ms); }
    } else {
        puts("Usage: route list | route show N | route stats [N|all] | route reset");
        puts("       route set N key=value ... | route enable N | route disable N | route trigger N");
        puts("       route loopwindow MS     (N = 1..20 or RT-01..RT-20)");
        puts("Keys: filter=ID/MASK filter_ext dec=raw|hex|ps_can enc=raw|hex|can_line|can|i2c|spi");
        puts("      term=none|lf|crlf map=pass|slice|scale slice=OFF:LEN scale=IW:be|le:s|u:MUL:DIV:ADD:OW:be|le:s|u");
        puts("      can_id=HEX|datum can_ext i2c_addr=HEX prefix=HEX|none poll_i2c=ADDR:WRITEHEX:LEN");
        puts("      poll_spi=HEX interval=MS policy=drop|hold hold_ms src_dev dst_dev");
        result = 1;
    }
    proto_route_runtime_unlock();
    return result;
}

esp_err_t proto_route_console_register(void)
{
    const esp_err_t err = proto_route_runtime_start();
    if (err != ESP_OK) { return err; }
    const esp_console_cmd_t command = {
        .command = "route",
        .help = "Phase 7 routing engine (RT-01..RT-20): list, show, set, enable, disable, trigger, stats",
        .func = route_command,
    };
    return esp_console_cmd_register(&command);
}
