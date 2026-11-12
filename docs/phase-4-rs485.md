# Phase 4 — RS-485 interface (UART2 + automatic-direction module)

**Milestone:** M4 — half-duplex RS-485 with a known node  
**Firmware:** 0.4.0  
**State:** implementation and host tests passed; ESP-IDF build and hardware acceptance pending

RS-485 is only the electrical layer. Phase 4 proves **raw serial over RS-485**
(LF-terminated ASCII lines, the same framing as the Phase 2 UART). **Modbus RTU
is not implemented**; per the roadmap it is validated separately, after raw
RS-485 works (Q-032 is still open). No I2C, SPI or conversion route is started
and RT-03/RT-04 stay disabled.

## Consistency with the Phase-0 documents
| Item | Document | Phase-4 firmware |
|---|---|---|
| RS-485 TX / RX pins | [pinout](pinout-and-connectors.md): GPIO15 / GPIO16 (preliminary) | `PROTO_PIN_RS485_TX/RX` in `proto_board.h` = GPIO15 / GPIO16 |
| UART + automatic-direction module (FR-003) | requirements | UART2 on those pins; no DE/RE pin driven; no `UART_MODE_RS485_*` mode selected |
| Half-duplex behaviour must be understood and verified (Phase 4 exit) | roadmap | TX window + guard discards own echo; `echo_bytes` measures what the module really does |
| Characterize the real module, keep it separate from protocol (risk table) | Phase-0 risks | `keepecho` start option exposes raw module behaviour; module checks are listed below |
| Component name `proto_rs485` | [Phase 1 layout](phase-1-foundation.md) | `components/proto_rs485/` |
| Pins parked until started | Phase 1 | RS-485 stays stopped until `rs485 start` |
| Non-blocking / counters (NFR-005) | requirements | counters for bytes, frames, echo, malformed, overlong, overflows, UART errors, TX failures |

## Firmware behaviour
- Component `proto_rs485`, UART2, started only by `rs485 start`; reboot to change.
- Rates 9600 (default), 19200, 38400, 57600, 115200; 8N1 only. The module's
  vendor claim of 110–256000 baud is **not** assumed; verify each rate you use.
- Framing: identical to Phase 2 (LF end, CRLF accepted, printable ASCII, max 64
  wire bytes, 200 ms partial timeout; malformed/overlong are counted).
- **Half-duplex handling:** from the start of a transmission until
  `2 character times + 1 ms` after the last bit, received bytes are counted in
  `echo_bytes` and discarded, so the module echoing your own TX can never appear
  as a received frame. Bytes from a peer that talks *during* that window are
  also lost: that is a collision in half-duplex and is not arbitrated.
  `rs485 start BAUD keepecho` turns the discard off so you can see the echo
  itself as a received frame.
- Transmit completion (`tx_completed`) means the UART drained, not that a peer
  received it.

## Console (UART0, 115200)
```text
rs485 start [9600|19200|38400|57600|115200] [keepecho]
rs485 status
rs485 send HEX            e.g. rs485 send 50494E470A  (sends "PING\n")
rs485 read                -> rs485_rx bytes=N hex=... time_us=...
```

## Pre-wiring gate (do not wire the module until done)
- [ ] Exact module marking/revision and its transceiver IC (Q-012); photo of both sides.
- [ ] Verified UART-side pin labels and meaning (Q-013). Labels like TXD/RXD on
  these boards are ambiguous; decide by the loopback check below, not a photo.
- [ ] Supply and logic: power it from **3V3** so its RXD output is 3.3 V
  (ESP32-S3 GPIO is not 5 V tolerant). Before connecting RXD to GPIO16, measure
  RXD idle with a multimeter: it must read about 3.3 V, never 5 V.
- [ ] What termination and bias the module fits (jumper/resistor/none) and the
  bus topology (NFR-004).
- [ ] The second node: a USB-RS485 adapter or another MCU + transceiver (Q-030).

## Test stages (power off before wiring)

### Stage A — firmware only, no module
One jumper wire GPIO15 to GPIO16. In the monitor:
```
rs485 start 115200 keepecho
rs485 send 50494E470A
rs485 read
```
Expect `rs485_rx bytes=5 hex=50494E470A`. This proves UART2 and pins only.
Remove the jumper afterwards.

### Stage B — module wired, one node on the bus
| ESP32-S3 | RS-485 module (TTL side) |
|---|---|
| GPIO15 (TX) | the module pin that takes data from the MCU (usually labelled TXD or DI/TX) |
| GPIO16 (RX) | the module pin that outputs data to the MCU (usually RXD or RO/RX) |
| 3V3 | VCC |
| GND | GND |

Bus side: module A to adapter A, module B to adapter B, grounds referenced
together, 120 ohm termination only at the two physical ends (check what is
already fitted; do not terminate every node). If the adapter has no bias, add
fail-safe bias per the bus design rather than guessing resistor values.

If `rs485 send` is received by the adapter as `PING`, the TX wiring is right.
If nothing arrives, check that TX goes to the module's data-in pin, then A/B
(a swapped A/B gives no or garbled data, not damage).

Characterize the module with the adapter silent:
```
rs485 start 9600
rs485 send 50494E470A
rs485 status
```
`echo_bytes=0` means the module does not echo; `echo_bytes=5` means it echoes
every TX byte (the firmware discards it). Record the result.

### Hardware script
```bash
python tools/test_rs485_fixture.py --console-port CONSOLE_PORT \
    --node-port /dev/cu.usbserial-NODE --baud 9600 \
    --log docs/test-reports/rs485-9600-hardware.log
```
It checks: foundation state; node-to-board lines (chunked and back-to-back);
board-to-node lines (including a 64-byte frame); a query/reply turnaround;
malformed and overlong input counted and recovered; zero UART error counters.
Repeat at each rate you intend to support (at least 9600 and 115200) and record
the module's measured echo behaviour from the log.

### Manual checks to record
- Disconnect A or B while sending: no crash, `tx_completed` still counts the UART
  drain (there is no peer acknowledgement), the node receives nothing.
- Swap A and B: no valid frames; reconnect and confirm normal traffic returns.
- Both sides transmit together: expect lost/malformed frames (collision), then recovery.
- Note idle-line noise: with no driver and no bias, spurious bytes can appear.

## Acceptance state
- [x] UART2 driver, half-duplex echo/guard handling and counters implemented.
- [x] Rate/timing helpers host-tested with sanitizers; framer shared with Phase 2.
- [x] Driver source syntax-checked against stand-in headers only.
- [ ] Builds against ESP-IDF v5.5.6 (`./tools/verify_phase4.sh`).
- [ ] Pre-wiring gate completed and recorded.
- [ ] Real-node TX and RX verified; half-duplex behaviour and echo measured per rate.
- [ ] Module wiring, logic level, termination and bias recorded.
M4 stays open until the hardware items pass. Modbus framing and CRC are a later,
separate step.
