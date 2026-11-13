#pragma once

/* Driver capability/role registry for the 20-route matrix (pure C table). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proto_event.h"

#define PROTO_CAP_RX_EVENT (1u << 0)              /* unsolicited receive -> events */
#define PROTO_CAP_TX (1u << 1)                    /* accepts a transmit request */
#define PROTO_CAP_CONTROLLER_TRANSACTION (1u << 2)/* ProtoScope initiates; result event returned */
#define PROTO_CAP_BYTE_STREAM (1u << 3)           /* needs a framing rule (LF line) */
#define PROTO_CAP_FRAME_ORIENTED (1u << 4)        /* frames arrive already delimited */
#define PROTO_CAP_HALF_DUPLEX (1u << 5)
#define PROTO_CAP_FULL_DUPLEX_TRANSFER (1u << 6)
#define PROTO_CAP_CONTROLLER_ROLE (1u << 7)       /* controller/host role only; no peripheral mode */

uint32_t proto_protocol_caps(proto_protocol_t protocol);
const char *proto_protocol_name(proto_protocol_t protocol);   /* "UART", "RS485", ... */
size_t proto_protocol_max_payload(proto_protocol_t protocol);
/* Case-insensitive; accepts "rs485" and "rs-485". */
bool proto_protocol_parse(const char *name, proto_protocol_t *protocol);
/* "RX|TX|..." into out; returns length. */
size_t proto_caps_format(uint32_t caps, char *out, size_t out_size);
