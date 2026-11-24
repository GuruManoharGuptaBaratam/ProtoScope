#include "proto_monitor.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "proto_can.h"
#include "proto_i2c.h"
#include "proto_route_runtime.h"
#include "proto_rs485.h"
#include "proto_spi.h"
#include "proto_storage.h"
#include "proto_uart.h"

static void note_u32(char *out, size_t size, uint32_t value)
{
    snprintf(out, size, "%u", (unsigned)value);
}

void proto_monitor_collect(proto_monitor_t *m)
{
    if (!m) { return; }
    memset(m, 0, sizeof(*m));
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(m->firmware, sizeof(m->firmware), "%.11s", app ? app->version : "?");
    m->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    m->free_heap = (uint32_t)esp_get_free_heap_size();
    m->min_free_heap = (uint32_t)esp_get_minimum_free_heap_size();
    m->psram_bytes = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    const proto_uart_snapshot_t u = proto_uart_get_snapshot();
    proto_monitor_iface_t *i = &m->iface[PROTO_PROTOCOL_UART];
    i->active = u.active; i->rx = u.rx_frames; i->tx = u.tx_completed;
    i->errors = u.malformed + u.overlong + u.frame_errors + u.parity_errors + u.fifo_overflows +
                u.buffer_full + u.tx_failures;
    i->drops = u.rx_queue_drops;
    if (u.active) { note_u32(i->note, sizeof(i->note), (uint32_t)u.baud_rate); }

    const proto_rs485_snapshot_t r = proto_rs485_get_snapshot();
    i = &m->iface[PROTO_PROTOCOL_RS485];
    i->active = r.active; i->rx = r.rx_frames; i->tx = r.tx_completed;
    i->errors = r.malformed + r.overlong + r.frame_errors + r.parity_errors + r.fifo_overflows +
                r.buffer_full + r.tx_failures;
    i->drops = r.rx_queue_drops;
    if (r.active) { note_u32(i->note, sizeof(i->note), r.baud_rate); }

    const proto_can_snapshot_t c = proto_can_get_snapshot();
    i = &m->iface[PROTO_PROTOCOL_CAN];
    i->active = c.active; i->rx = c.rx_frames; i->tx = c.tx_completed;
    i->errors = c.tx_failed + c.rx_missed + c.rx_overrun;
    i->drops = c.rx_queue_drops;
    i->warning = c.active && (c.state == PROTO_CAN_STATE_BUS_OFF || c.state == PROTO_CAN_STATE_RECOVERING ||
                              c.tx_error_counter > 127 || c.rx_error_counter > 127);
    if (c.active) {
        snprintf(i->note, sizeof(i->note), "%uk%s", (unsigned)(c.bit_rate / 1000),
                 c.mode == PROTO_CAN_MODE_LISTEN_ONLY ? "L" : c.mode == PROTO_CAN_MODE_SELF_TEST ? "S" : "");
    }

    const proto_i2c_snapshot_t x = proto_i2c_get_snapshot();
    i = &m->iface[PROTO_PROTOCOL_I2C];
    i->active = x.active; i->rx = x.completed; i->tx = x.transactions;
    i->errors = x.nacks + x.timeouts + x.bus_errors;
    i->drops = x.rejected;
    if (x.active) { snprintf(i->note, sizeof(i->note), "%uk", (unsigned)(x.frequency_hz / 1000)); }

    const proto_spi_snapshot_t s = proto_spi_get_snapshot();
    i = &m->iface[PROTO_PROTOCOL_SPI];
    i->active = s.active; i->rx = s.completed; i->tx = s.transfers;
    i->errors = s.failures;
    i->drops = s.rejected;
    if (s.active) { snprintf(i->note, sizeof(i->note), "M%u", (unsigned)s.mode); }

    proto_route_runtime_lock();
    const proto_route_engine_t *e = proto_route_runtime_engine();
    for (size_t k = 0; k < PROTO_ROUTE_COUNT; ++k) {
        const proto_route_slot_t *slot = &e->routes[k];
        m->route_src[k] = (uint8_t)slot->config.source;
        m->route_dst[k] = (uint8_t)slot->config.destination;
        m->route_enabled[k] = slot->config.enabled;
        m->route_ready[k] = proto_route_validate(&slot->config) == PROTO_ROUTE_OK;
        m->routes_enabled += slot->config.enabled;
        m->routes_ready += m->route_ready[k];
        m->route_transmitted += slot->stats.transmitted;
        m->route_rejected += slot->stats.rejected_decode + slot->stats.rejected_map +
                             slot->stats.rejected_encode + slot->stats.dest_failures +
                             slot->stats.dropped_busy + slot->stats.timed_out;
    }
    proto_route_runtime_unlock();

    const proto_storage_snapshot_t l = proto_storage_get_snapshot();
    m->log_state = l.state;
    m->log_wanted = l.wanted;
    m->card_mounted = l.card_mounted;
    snprintf(m->log_file, sizeof(m->log_file), "%s", l.file_name);
    m->log_records = l.records_written;
    m->log_dropped = l.dropped_total;
    m->log_write_errors = l.write_errors;
}
