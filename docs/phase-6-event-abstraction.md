# Phase 6 — Common event and protocol abstraction

**Milestone:** M6 — required metadata preserved for all five interfaces  
**Firmware:** 0.6.0  
**State:** implemented and host-tested (no hardware needed for the core)

Phase 6 adds **no new interface and no routing**. It defines the shared event
envelope and the driver contract that Phase 7's route engine will use, so that a
new source/destination pairing needs only a route definition plus the decoder,
mapping and encoder, not a new pair-specific pipeline. Component: `proto_event`
(the name from the [Phase 1 layout](phase-1-foundation.md)); the type is
`proto_event_t` (the roadmap's `proto_message_t` sketch).

## Envelope (matches system-architecture §4)
| Requirement | Field |
|---|---|
| event and correlation identifiers | `event_id`, `correlation_id` (a result carries the request's id) |
| source interface and configured device | `protocol`, `device_id` |
| monotonic timestamp | `timestamp_us` (boot-relative, FR-033) |
| direction, status, error code | `direction` (RX/TX), `status` (OK/TIMEOUT/NACK/BUS_ERROR/INVALID/REJECTED), `error_code` |
| payload and length | `payload[64]`, `payload_length` |
| route provenance / hop count | `hop_count`, `origin_route_id`, `from_protoscope` |
| protocol-specific metadata | `meta` (a union tagged by `protocol`) |

Metadata kept per interface (never reduced to bytes only):
- **UART / RS-485:** `baud_rate`, `terminator` (NONE/LF/CRLF) so CR/LF is not lost.
- **CAN:** `id`, `extended`, `rtr`, `dlc`.
- **I2C:** `address`, `op` (WRITE/READ/WRITE_READ), `write_length`, `read_length`, `ack` (ACK/NACK/unknown). A request carries the write bytes (register/command bytes included); a result carries the bytes read.
- **SPI:** `chip_select`, `mode`, `clock_hz`, `word_bits` (8), `transaction_length`. A request carries MOSI; a result carries MISO of equal length.

A request may leave serial `baud_rate` or SPI `clock_hz` at 0, meaning "the
driver's configured value"; the result reports the value actually used.

## Message and transaction boundaries
- **UART / RS-485** are byte streams. The boundary is the framing rule from
  Phases 2/4: LF-terminated printable-ASCII lines, max 64 wire bytes, 200 ms
  partial timeout. One framed line = one event, every wire byte preserved.
- **CAN** frames arrive already delimited: one frame = one event.
- **I2C / SPI** boundaries are the controller transaction ProtoScope starts: one
  transaction = one request event and one result event.

## Ownership and lifetime
An event **owns an inline copy** of its payload (no pointers). Events are passed
and queued by value, so no lifetime depends on a driver buffer and an event is
safe to hand to another task. The caller owns the events it passes to
`submit()`/`receive()`.

## Provenance and loop prevention
Events from external traffic have `hop_count 0`, `from_protoscope false`. A
transmission ProtoScope creates for a route is derived with
`proto_event_mark_forwarded()` (hop + 1, `from_protoscope`, `origin_route_id`,
`correlation_id` = source id). `proto_event_loop_rejected()` is true for ProtoScope's own
transmissions and for `hop_count >= 4`, so a transmitted event can never be
treated as a fresh source (FR-016). Phase 7 applies this at route entry.

## Driver contract and capability registry
Every driver is reachable through `proto_driver_get(protocol)`:
`receive()` (UART, RS-485, CAN only; NULL for I2C/SPI) and `submit(request, result)`.
For I2C/SPI a "read" is a controller transaction submitted as a request; the
result event carries the data, NACK or error. The capability table (pure C,
tested) records for each interface: unsolicited RX, TX, controller transaction,
byte-stream framing, half/full duplex, controller-only role, max payload (UART/RS-485
64, CAN 8, I2C 64, SPI 64). It is the registry the Phase-7 router will consult.

## Validation
`proto_event_validate()` rejects: unknown protocol/direction/status, payload longer
than the protocol allows, inconsistent metadata (CAN DLC/ID/RTR, I2C address or
operation shape, SPI mode/clock/length, serial terminator not matching the bytes),
RX events without a baud rate, and `hop_count` over the limit.

## Console (needs the interface started first)
```text
msg caps
msg poll uart|rs485|can
msg send uart|rs485 HEX        msg send can ID#HEX        msg send spi HEX
msg send i2c ADDR w HEX        msg send i2c ADDR r LEN    msg send i2c ADDR wr HEX LEN
```
`msg poll` on I2C/SPI reports "controller only; use msg send". Output is the
one-line event text (`event id=... proto=... dir=... status=... | metadata`).

## Verification
`./tools/test_host.sh` builds and runs, with sanitizers: event round trips for
all five interfaces (metadata preserved, CRLF/LF kept, extended/RTR CAN, I2C
three operations plus NACK, SPI MOSI/MISO), validation rejections, provenance
and loop rules, capability table and text form. Hardware-assisted checks:
`tools/test_phase5_6_fixture.py` with `--spi-loopback` and `--can-selftest`
(jumper GPIO4-GPIO5 or a transceiver) exercises the same path through the real drivers.

## Acceptance state
- [x] Envelope, per-protocol metadata, validation, provenance and capability registry implemented.
- [x] Driver adapters for all five interfaces behind one contract.
- [x] Unit vectors for every interface (host, sanitizers).
- [ ] Builds against ESP-IDF v5.5.6 (`./tools/verify_phase6.sh`).
- [ ] `msg` console path exercised with real drivers (SPI loopback, CAN self-test, real I2C).
M6 is complete when the build passes and the console checks pass. It does not
depend on external fixtures; Phase 7 starts from this contract.
