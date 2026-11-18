# Phase 7 — Configurable many-to-many routing and conversion engine

**Milestone:** M7 — all 20 directed routes defined, implemented and passing their own tests  
**Firmware:** 0.7.0  
**State:** engine implemented and host-tested for all 20 routes with structural vectors; **M7 is NOT complete** (device-level definitions and hardware tests are still required per route)

Phase 7 builds on the Phase 6 event envelope and driver contract. It does not
claim that arbitrary device protocols are understood, that all 20 routes run at
once, or that any route has passed hardware evidence yet. Modbus RTU is not
implemented (route-matrix Q-032 is open).

## What exists
- `components/proto_route` — a pure-C engine (host-tested with mock interfaces) plus a runtime that
  binds it to the real drivers through `proto_driver_get()`, and a console `route`.
- A **route registry** of exactly the 20 directed routes RT-01..RT-20 numbered as in
  [`conversion-mappings/route-matrix.md`](conversion-mappings/route-matrix.md). All start **disabled**.
  There are no 20 hand-written forwarding functions: each route is a record that selects a
  source filter or scheduled read, a decoder, a mapper, an encoder, and a policy.
- A route can only be enabled when it validates; otherwise it stays disabled and the console says why
  (`can_filter_not_set`, `source_read_not_configured`, `destination_not_configured`, ...).
  Out of the box only RT-01, RT-03 and RT-04 are enable-able; every other route is "not configured"
  until its device-level details are set (FR-013).

## Pipeline per route
`receive -> match -> decode -> map -> encode -> transmit -> count`
- **Sources:** UART, RS-485 and CAN deliver events (the engine drains an interface only if an enabled
  route uses it, so `msg poll` still works otherwise). I2C and SPI sources are **scheduled controller
  reads** (`poll_i2c`, `poll_spi`, `interval` or `route trigger N`); the result event is the source.
- **Match:** CAN source routes require an explicit filter `id/mask` (route-matrix: filter TBD).
- **Decoders:** `raw` (payload, serial terminator removed), `hex` (ASCII hex line to bytes),
  `ps_can` (the RT-01 grammar `CAN,<id_hex>,<dlc>,<payload_hex>`).
- **Mappers:** `pass`, `slice` (offset:length), `scale` (integer `v' = v*mul/div + add`, widths 1/2/4,
  either endianness, signed/unsigned, **range-checked** so an out-of-range value is rejected, never
  wrapped). Raw forwarding stays distinct from semantic mapping.
- **Encoders:** serial `raw` / `hex` / `can_line`, `can` frame, `i2c` write (prefix + data), `spi`
  transfer (prefix + data on MOSI). I2C/SPI destinations require the command bytes to be chosen
  explicitly (`prefix=none` counts) and I2C needs a valid address; a CAN destination needs an ID
  (`can_id=HEX`) or `can_id=datum` with the `ps_can` decoder.
- **Policy:** destination busy -> `drop` (default: reject and count) or `hold` (retry until
  `hold_ms`, then count `timed_out`). While a request is held, the source interface is not drained
  (back-pressure); data then waits in the driver's own bounded queue, whose overflow is counted
  there (`rx_queue_drops`), never silently.

## RT-01 (the fully specified route)
UART RX `115200 8N1` line `CAN,<id_hex>,<dlc>,<payload_hex>\n` to a Classic CAN standard data frame.
`<id_hex>` is exactly 3 hex digits, 000-7FF; `<dlc>` 0-8; payload exactly `2*dlc` hex digits;
case-insensitive; CRLF accepted; no whitespace or extra fields. Required negative vectors are in the
host test: id out of range (`800`), payload/DLC mismatch, invalid id (`XYZ`), DLC 9, extra field,
whitespace, bad hex, short id, wrong prefix. A partial line never reaches the route (the Phase 2
framer expires it after 200 ms). RT-01 is enable-able by default, but it stays **disabled** until you enable it.

## Loop and duplicate re-entry prevention (FR-016)
- Every request a route creates is marked `from_protoscope`, hop + 1, origin route, correlation id.
  `proto_event_loop_rejected()` refuses such events and events at the hop limit (4) as sources.
- Because drivers cannot tell their own reflected transmission from external traffic (CAN self-reception,
  a UART or RS-485 echo), the engine also remembers a fingerprint (interface, ID, payload) of each
  UART/RS-485/CAN transmission for `loop_window_ms` (default 100 ms, `route loopwindow MS`, 0 disables)
  and suppresses a matching received event, counted as `loop_suppressed`. Trade-off: an external node
  sending the identical frame inside that window right after ProtoScope is suppressed too.

## Route status (what is done and what is still TBD)
Host vectors use fake devices only. "Needs" lists what you must define before enabling.

| Route | Direction | Default profile | Needs before enable | Host test | Hardware |
|---|---|---|---|---|---|
| RT-01 | UART -> CAN | `ps_can` -> CAN frame, id from datum | nothing (RT-01 grammar) | pass (+ negatives) | pending |
| RT-02 | CAN -> UART | raw -> `CAN,id,dlc,hex` line | CAN filter | pass | pending |
| RT-03 | UART -> RS-485 | raw line passthrough (LF) | nothing | pass | pending |
| RT-04 | RS-485 -> UART | raw line passthrough (LF) | nothing | pass | pending |
| RT-05 | CAN -> RS-485 | raw -> `CAN,id,dlc,hex` line | CAN filter | pass | pending |
| RT-06 | RS-485 -> CAN | `ps_can` -> CAN frame | `can_id=datum` or fixed id | pass | pending |
| RT-07 | UART -> I2C | hex line -> I2C write | address, prefix | pass | pending |
| RT-08 | I2C -> UART | scheduled read -> hex line | `poll_i2c` | pass | pending |
| RT-09 | CAN -> I2C | CAN payload -> I2C write | filter, address, prefix | pass | pending |
| RT-10 | I2C -> CAN | scheduled read -> CAN frame | `poll_i2c`, CAN id | pass (+scaling) | pending |
| RT-11 | RS-485 -> I2C | hex line -> I2C write | address, prefix | pass | pending |
| RT-12 | I2C -> RS-485 | scheduled read -> hex line | `poll_i2c` | pass | pending |
| RT-13 | UART -> SPI | hex line -> SPI transfer | prefix | pass | pending |
| RT-14 | SPI -> UART | scheduled read -> hex line | `poll_spi` | pass | loopback test available |
| RT-15 | CAN -> SPI | CAN payload -> SPI transfer | filter, prefix | pass | pending |
| RT-16 | SPI -> CAN | scheduled read -> CAN frame | `poll_spi`, CAN id | pass | pending |
| RT-17 | RS-485 -> SPI | hex line -> SPI transfer | prefix | pass | pending |
| RT-18 | SPI -> RS-485 | scheduled read -> hex line | `poll_spi` | pass | pending |
| RT-19 | I2C -> SPI | scheduled read -> SPI transfer | `poll_i2c`, prefix | pass | pending |
| RT-20 | SPI -> I2C | scheduled read -> I2C write | `poll_spi`, address, prefix | pass | pending |

The serial-line schemas for RT-02/05 (`CAN,<id>,<dlc>,<hex>`; 3 digits standard, 8 digits extended; RTR
frames are rejected as unrepresentable) and the hex-line schemas for RT-07/11/13/17 and RT-08/12/14/18 are
**proposals** to be confirmed against your real devices (they are marked TBD in the route matrix).
RT-04 re-terminates lines with LF unless you set `term=`. Modbus is not part of any route.

## Console (UART0, 115200)
```text
route list                       route show N               route stats [N|all]     route reset
route set N key=value ...        route enable N             route disable N
route trigger N                  route loopwindow MS        (N = 1..20 or RT-01..RT-20)
```
Keys: `filter=ID/MASK filter_ext=0|1 dec=raw|hex|ps_can enc=raw|hex|can_line|can|i2c|spi term=none|lf|crlf
map=pass|slice|scale slice=OFF:LEN scale=IW:be|le:s|u:MUL:DIV:ADD:OW:be|le:s|u can_id=HEX|datum can_ext=0|1
i2c_addr=HEX prefix=HEX|none poll_i2c=ADDR:WRITEHEX:LEN poll_spi=HEX interval=MS policy=drop|hold hold_ms=N
src_dev=N dst_dev=N`.
Examples: `route set 2 filter=123/7FF` then `route enable 2`;
`route set 10 poll_i2c=68:75:2 can_id=200 scale=2:be:u:1:10:0:2:be:u interval=500` then `route enable 10`.
A running route cannot be edited into an invalid state; `route disable N` first.
`route stats` shows per-route counters (matched, filtered, transmitted, rejected by stage, dest_unavailable,
dest_failures, dropped_busy, timed_out, polls, poll_failures, last_error, last_reject) and per-interface
counters (rx_events, loop_suppressed, loop_rejected, tx_submitted). `status` shows the route summary.

## Verification
`./tools/test_host.sh` (sanitizers) now also runs the routing test: RT-01 positive and negative vectors, the
20-route table (every directed pair exactly once, none same-interface), configuration parsing, mappers,
encoders, **all 20 directed routes end to end through the engine with mock interfaces**, filtering,
rejection counters, disabled/unconfigured/inactive handling, DROP / HOLD / timeout / failure policies,
fan-out and back-pressure, loop suppression and provenance guard, polling (interval, trigger, failure).

Hardware (no external devices, three jumpers: GPIO17-18, GPIO4-5, GPIO40-41):
```bash
python tools/test_phase7_fixture.py --console-port CONSOLE_PORT --log docs/test-reports/phase7-loopback.log
```
It runs RT-01 end to end on real drivers, rejects the negative vectors, shows the RT-01/RT-02 loop being
stopped, and runs RT-14 (SPI -> UART). It does not replace device-level tests of the other routes.

## Not covered (be explicit when reporting)
- Capacity: all routes are **not** expected to run at the same time. The engine runs in one task (about
  10 ms cadence, up to 16 events per interface per pass). Simultaneous throughput is a separate capacity test.
- Persistence: routes are configured at runtime and are lost on reboot (Q-005: compiled-in vs runtime
  profiles is still open). Re-enter them or put them in firmware defaults later.
- Modbus RTU, CAN FD, passive I2C/SPI sniffing and I2C/SPI peripheral mode.

## Acceptance state
- [x] Route registry, configuration model and enable validation for the 20 directed routes.
- [x] Reusable decoder / mapper / encoder stages; no pair-specific application code.
- [x] Queue policy, back-pressure, timeouts, loop prevention, per-route and per-interface counters.
- [x] All 20 directed routes pass structural host vectors; RT-01 negative vectors pass.
- [ ] Builds against ESP-IDF v5.5.6 (`./tools/verify_phase7.sh`).
- [ ] Loopback hardware test passes on the board.
- [ ] Each route has a device-level definition (addresses, registers, IDs, filters, schemas) recorded in the route matrix.
- [ ] Each route has its own passing hardware test, including I2C <-> SPI both ways.
- [ ] Buffering/timeouts/errors tested with real devices; full 20-route regression after integration.
M7 stays open until the hardware and device-level items pass.
