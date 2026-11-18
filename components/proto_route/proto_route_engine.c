#include "proto_route.h"

#include <string.h>

static bool is_event_source(proto_protocol_t p)
{
    return p == PROTO_PROTOCOL_UART || p == PROTO_PROTOCOL_RS485 || p == PROTO_PROTOCOL_CAN;
}

static bool is_polled_source(proto_protocol_t p)
{
    return p == PROTO_PROTOCOL_I2C || p == PROTO_PROTOCOL_SPI;
}

/* ---- loop prevention: remember what ProtoScope itself transmitted ----------------- */
static uint32_t fingerprint_of(const proto_event_t *event)
{
    uint32_t h = 2166136261u;
    const uint8_t head[6] = { (uint8_t)event->protocol,
        (uint8_t)(event->protocol == PROTO_PROTOCOL_CAN ? (event->meta.can.extended | (event->meta.can.rtr << 1)) : 0),
        (uint8_t)(event->protocol == PROTO_PROTOCOL_CAN ? event->meta.can.id >> 24 : 0),
        (uint8_t)(event->protocol == PROTO_PROTOCOL_CAN ? event->meta.can.id >> 16 : 0),
        (uint8_t)(event->protocol == PROTO_PROTOCOL_CAN ? event->meta.can.id >> 8 : 0),
        (uint8_t)(event->protocol == PROTO_PROTOCOL_CAN ? event->meta.can.id : 0) };
    for (size_t i = 0; i < sizeof(head); ++i) { h = (h ^ head[i]) * 16777619u; }
    for (size_t i = 0; i < event->payload_length; ++i) { h = (h ^ event->payload[i]) * 16777619u; }
    return h;
}

static void record_transmission(proto_route_engine_t *e, const proto_event_t *request, uint64_t now)
{
    if (!is_event_source(request->protocol) || e->loop_window_ms == 0) { return; }
    proto_fingerprint_t *slot = &e->fingerprints[e->fingerprint_next];
    slot->protocol = (uint8_t)request->protocol;
    slot->hash = fingerprint_of(request);
    slot->time_us = now;
    slot->valid = true;
    e->fingerprint_next = (uint8_t)((e->fingerprint_next + 1u) % PROTO_ROUTE_FINGERPRINTS);
}

/* True (and the record is consumed) when an RX event is ProtoScope's own recent transmission. */
static bool is_own_echo(proto_route_engine_t *e, const proto_event_t *event, uint64_t now)
{
    if (e->loop_window_ms == 0) { return false; }
    const uint32_t hash = fingerprint_of(event);
    const uint64_t window = (uint64_t)e->loop_window_ms * 1000u;
    for (size_t i = 0; i < PROTO_ROUTE_FINGERPRINTS; ++i) {
        proto_fingerprint_t *fp = &e->fingerprints[i];
        if (!fp->valid) { continue; }
        if (now - fp->time_us > window) { fp->valid = false; continue; }
        if (fp->protocol == (uint8_t)event->protocol && fp->hash == hash) {
            fp->valid = false;
            return true;
        }
    }
    return false;
}

/* ---- engine ------------------------------------------------------------------------- */
void proto_route_engine_init(proto_route_engine_t *e)
{
    if (!e) { return; }
    memset(e, 0, sizeof(*e));
    e->loop_window_ms = PROTO_ROUTE_DEFAULT_LOOP_WINDOW_MS;
    for (uint8_t id = 1; id <= PROTO_ROUTE_COUNT; ++id) {
        proto_route_defaults(&e->routes[id - 1].config, id);
    }
}

proto_route_t *proto_route_engine_route(proto_route_engine_t *e, uint8_t id)
{
    return (e && id >= 1 && id <= PROTO_ROUTE_COUNT) ? &e->routes[id - 1].config : NULL;
}

const proto_route_stats_t *proto_route_engine_stats(const proto_route_engine_t *e, uint8_t id)
{
    return (e && id >= 1 && id <= PROTO_ROUTE_COUNT) ? &e->routes[id - 1].stats : NULL;
}

proto_route_check_t proto_route_engine_enable(proto_route_engine_t *e, uint8_t id)
{
    proto_route_t *route = proto_route_engine_route(e, id);
    if (!route) { return PROTO_ROUTE_ERR_ID; }
    const proto_route_check_t check = proto_route_validate(route);
    if (check != PROTO_ROUTE_OK) {
        route->enabled = false;
        return check;
    }
    route->enabled = true;
    proto_route_slot_t *slot = &e->routes[id - 1];
    slot->next_poll_us = 0;
    return PROTO_ROUTE_OK;
}

void proto_route_engine_disable(proto_route_engine_t *e, uint8_t id)
{
    proto_route_t *route = proto_route_engine_route(e, id);
    if (!route) { return; }
    route->enabled = false;
    e->routes[id - 1].pending = false;
    e->routes[id - 1].triggered = false;
}

bool proto_route_engine_trigger(proto_route_engine_t *e, uint8_t id)
{
    proto_route_t *route = proto_route_engine_route(e, id);
    if (!route || !route->enabled || !is_polled_source(route->source)) { return false; }
    e->routes[id - 1].triggered = true;
    return true;
}

void proto_route_engine_reset_stats(proto_route_engine_t *e)
{
    if (!e) { return; }
    for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) { memset(&e->routes[i].stats, 0, sizeof(e->routes[i].stats)); }
    memset(e->iface, 0, sizeof(e->iface));
    e->steps = 0;
}

/* Submits a request for one route and accounts for the outcome. */
static void deliver(proto_route_engine_t *e, const proto_route_io_t *io, proto_route_slot_t *slot,
                    const proto_event_t *request, bool was_pending, uint64_t now)
{
    proto_route_t *route = &slot->config;
    proto_route_stats_t *st = &slot->stats;
    proto_event_t result;
    int32_t error = 0;
    memset(&result, 0, sizeof(result));
    const proto_io_status_t status = io->submit(io->context, route->destination, request, &result, &error);
    if (status == PROTO_IO_OK) {
        st->transmitted++;
        e->iface[route->destination].tx_submitted++;
        record_transmission(e, request, now);
        slot->pending = false;
        return;
    }
    st->last_error = error;
    if (status == PROTO_IO_BUSY) {
        if (route->policy == PROTO_POLICY_HOLD) {
            if (!was_pending) {
                slot->pending = true;
                slot->pending_request = *request;
                slot->pending_since_us = now;
            } else if (now - slot->pending_since_us >= (uint64_t)route->hold_timeout_ms * 1000u) {
                st->timed_out++;
                slot->pending = false;
            }
        } else {
            st->dropped_busy++;
            slot->pending = false;
        }
        return;
    }
    st->dest_failures++;
    slot->pending = false;
}

static void dispatch(proto_route_engine_t *e, const proto_route_io_t *io, proto_route_slot_t *slot,
                     const proto_event_t *event, uint64_t now)
{
    proto_route_t *route = &slot->config;
    proto_route_stats_t *st = &slot->stats;
    if (!proto_route_match(route, event)) {
        st->filtered++;
        return;
    }
    st->matched++;
    proto_event_t request;
    const proto_proc_result_t result = proto_route_process(route, event, &request);
    if (result != PROTO_PROC_OK) {
        st->last_reject = result;
        if (result <= PROTO_PROC_DECODE_LENGTH) { st->rejected_decode++; }
        else if (result <= PROTO_PROC_MAP_RANGE) { st->rejected_map++; }
        else { st->rejected_encode++; }
        return;
    }
    request.event_id = io->next_id(io->context);
    if (!io->is_active(io->context, route->destination)) {
        st->dest_unavailable++;
        return;
    }
    deliver(e, io, slot, &request, false, now);
}

static void retry_pending(proto_route_engine_t *e, const proto_route_io_t *io, uint64_t now)
{
    for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) {
        proto_route_slot_t *slot = &e->routes[i];
        if (!slot->pending) { continue; }
        if (!slot->config.enabled || !io->is_active(io->context, slot->config.destination)) {
            if (now - slot->pending_since_us >= (uint64_t)slot->config.hold_timeout_ms * 1000u) {
                slot->stats.timed_out++;
                slot->pending = false;
            }
            continue;
        }
        deliver(e, io, slot, &slot->pending_request, true, now);
    }
}

static void poll_sources(proto_route_engine_t *e, const proto_route_io_t *io, uint64_t now)
{
    for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) {
        proto_route_slot_t *slot = &e->routes[i];
        proto_route_t *route = &slot->config;
        if (!route->enabled || !is_polled_source(route->source) || slot->pending) { continue; }
        const bool due = route->poll_interval_ms > 0 && now >= slot->next_poll_us;
        if (!slot->triggered && !due) { continue; }
        slot->triggered = false;
        if (route->poll_interval_ms > 0) { slot->next_poll_us = now + (uint64_t)route->poll_interval_ms * 1000u; }
        if (!io->is_active(io->context, route->source) ||
            !io->is_active(io->context, route->destination)) {
            slot->stats.dest_unavailable++;
            continue;
        }
        proto_event_t request, result;
        bool built;
        if (route->source == PROTO_PROTOCOL_I2C) {
            built = proto_event_from_i2c_request(&request, &route->poll_i2c);
        } else {
            built = proto_event_from_spi_request(&request, route->poll_spi, route->poll_spi_length, 0, 0);
        }
        if (!built) { slot->stats.poll_failures++; continue; }
        request.event_id = io->next_id(io->context);
        request.device_id = route->source_device;
        memset(&result, 0, sizeof(result));
        int32_t error = 0;
        slot->stats.polls++;
        if (io->submit(io->context, route->source, &request, &result, &error) != PROTO_IO_OK ||
            result.status != PROTO_EVENT_STATUS_OK) {
            slot->stats.poll_failures++;
            slot->stats.last_error = error;
            continue;
        }
        e->iface[route->source].rx_events++;
        result.timestamp_us = now;
        dispatch(e, io, slot, &result, now);
    }
}

static void receive_sources(proto_route_engine_t *e, const proto_route_io_t *io, uint64_t now)
{
    static const proto_protocol_t sources[] = { PROTO_PROTOCOL_UART, PROTO_PROTOCOL_RS485, PROTO_PROTOCOL_CAN };
    for (size_t s = 0; s < sizeof(sources) / sizeof(sources[0]); ++s) {
        const proto_protocol_t protocol = sources[s];
        bool used = false, blocked = false;
        for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) {
            const proto_route_slot_t *slot = &e->routes[i];
            if (slot->config.enabled && slot->config.source == protocol) {
                used = true;
                blocked = blocked || slot->pending;
            }
        }
        /* Only drain interfaces a route uses, so console 'msg poll' still works otherwise. */
        if (!used || blocked || !io->is_active(io->context, protocol)) { continue; }
        for (unsigned n = 0; n < PROTO_ROUTE_RX_BUDGET; ++n) {
            proto_event_t event;
            if (io->receive(io->context, protocol, &event) != PROTO_IO_OK) { break; }
            e->iface[protocol].rx_events++;
            if (is_own_echo(e, &event, now)) { e->iface[protocol].loop_suppressed++; continue; }
            if (proto_event_loop_rejected(&event)) { e->iface[protocol].loop_rejected++; continue; }
            bool back_pressure = false;
            for (size_t i = 0; i < PROTO_ROUTE_COUNT; ++i) {
                proto_route_slot_t *slot = &e->routes[i];
                if (!slot->config.enabled || slot->config.source != protocol) { continue; }
                dispatch(e, io, slot, &event, now);
                back_pressure = back_pressure || slot->pending;
            }
            if (back_pressure) { break; } /* leave the rest queued in the driver, explicitly counted there */
        }
    }
}

void proto_route_engine_step(proto_route_engine_t *e, const proto_route_io_t *io)
{
    if (!e || !io || !io->is_active || !io->receive || !io->submit || !io->now_us || !io->next_id) { return; }
    const uint64_t now = io->now_us(io->context);
    e->steps++;
    retry_pending(e, io, now);
    poll_sources(e, io, now);
    receive_sources(e, io, now);
}
