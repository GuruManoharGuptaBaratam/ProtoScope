#pragma once

/* Common driver contract (Phase 6): every interface driver is reachable through
 * the same two operations, so Phase 7 routing needs no pair-specific code.
 *
 *  - receive(): non-blocking; fills one RX event if the interface has unsolicited
 *    receive (UART, RS-485, CAN). NULL for I2C/SPI, which ProtoScope reads by
 *    running a controller transaction (see submit).
 *  - submit(request, result): a TX request. For UART/RS-485/CAN, `result` (may be
 *    NULL) is a TX completion event. For I2C/SPI, `result` carries the bytes read
 *    (or the NACK/error status) and correlation_id = request->event_id.
 * Events are copied by value; the caller owns both events. */

#include "esp_err.h"
#include "proto_event.h"
#include "proto_event_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    proto_protocol_t protocol;
    const char *name;
    uint32_t caps;
    size_t max_payload;
    bool (*is_active)(void);
    esp_err_t (*receive)(proto_event_t *event);
    esp_err_t (*submit)(const proto_event_t *request, proto_event_t *result);
} proto_driver_t;

/* NULL for an invalid protocol. */
const proto_driver_t *proto_driver_get(proto_protocol_t protocol);
/* Observer tap (Phase 8): called for every RX event delivered through receive(), for
 * every TX completion event, and for I2C/SPI request + result events, no matter who
 * (route engine, console, monitor) used the driver contract. NULL disables it. The
 * callback runs in the caller's task and must be fast and non-blocking. */
typedef void (*proto_event_observer_t)(const proto_event_t *event);
void proto_event_set_observer(proto_event_observer_t observer);

/* Unique, monotonically increasing event ids (never 0). */
uint64_t proto_event_next_id(void);

#ifdef __cplusplus
}
#endif
