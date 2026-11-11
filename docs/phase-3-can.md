# Phase 3 — CAN interface (ESP32-S3 TWAI + SN65HVD230)

**Milestone:** M3 — independent Classic CAN TX/RX with a real node  
**Firmware:** 0.3.0  
**State:** implementation and host tests passed; ESP-IDF build and hardware acceptance pending

The revised roadmap defines Phase 3 as CAN alone. The SN65HVD230 is only the CAN
*transceiver* (physical layer); the ESP32-S3 TWAI peripheral is the CAN
*controller*, so no MCP2515 is used. No RS-485, I2C, SPI or conversion route is
started, and RT-01 stays disabled.

## Consistency with the Phase-0 documents
| Item | Document | Phase-3 firmware |
|---|---|---|
| TWAI TX / RX pins | [pinout](pinout-and-connectors.md): GPIO4 / GPIO5 | `PROTO_PIN_TWAI_TX/RX` in `proto_board.h` = GPIO4 / GPIO5 (single source of truth) |
| Classic CAN only (FR-002, no CAN FD) | requirements | Classic CAN, DLC 0-8 |
| Initial rate 500 kbit/s (RT-01) | route matrix | default 500 k; 125 k / 250 k / 1 M selectable |
| Standard 11-bit data frame for RT-01 | route matrix | supported; extended IDs and remote frames are an additional driver capability, not an RT-01 requirement |
| Non-blocking, counters (NFR-005) | requirements | TX never blocks (full queue -> `tx_rejected`); RX uses a 16-frame application queue with `rx_queue_drops` |
| No blind retry; no success logged before driver reports (RT-01) | route matrix | recovery is left to the TWAI controller; `tx_completed` counts only ACKed frames |
| Boot-relative monotonic time (FR-033) | requirements | frame timestamp = `esp_timer_get_time()` |
| Component name `proto_twai` | [Phase 1 layout](phase-1-foundation.md) | `components/proto_twai/` |
| Pins stay parked until started | Phase 1 | CAN stays stopped until `can start` |
| Receive filter "TBD" (RT-02) | route matrix | accept-all; filtering is deferred to the route phase |

## Pre-wiring gate (do not wire until done)
The [pinout document](pinout-and-connectors.md) is still a draft. Before
connecting the CAN module record, in the pinout/BOM documents:
- [ ] Exact CAN breakout SKU, schematic and pin labels (Q-010).
- [ ] Whether it fits fixed/switchable 120 ohm termination and how the
  SN65HVD230 `Rs` pin is configured (Q-011).
- [ ] Module supply is 3.3 V and logic is 3.3 V (NFR-001/NFR-002).
- [ ] The independent CAN node/analyzer to test against (Q-030).
- [ ] Carrier header check that GPIO4 and GPIO5 are exposed.
- [ ] Phase 1/2 physical acceptance (M1/M2) is recommended first.

## Wiring (after the gate; power off)
| ESP32-S3 | CAN module | Note |
|---|---|---|
| GPIO4 (TWAI TX) | TXD / D / CTX | verify the label on your exact board |
| GPIO5 (TWAI RX) | RXD / R / CRX | verify the label on your exact board |
| 3V3 | VCC | only if the module is a 3.3 V part |
| GND | GND | common ground with the test node |
| - | CANH, CANL | to the test node |

Terminate with 120 ohm only at the two physical ends of the bus; do not enable
termination on every node (NFR-004). Never connect 5 V to a GPIO.

## Firmware behaviour
- `components/proto_twai`, started only by `can start`; reboot to stop or change.
- Bit rates 125 k, 250 k, 500 k (default), 1 M. 11-bit and 29-bit identifiers,
  data and remote frames, DLC 0-8. Accept-all filter.
- Modes: `normal`, `listen` (never drives the bus), `selftest` (no-ACK loopback
  for a single-board driver check; it does **not** satisfy M3).
- A task drains the TWAI queue into a 16-frame application queue. TX queue is 8
  deep; `can send` returns immediately.
- Error-passive and bus-off are counted and logged; bus-off recovery is automatic.
  TEC/REC, arbitration loss and bus errors are shown by `can status`.
- With no other ACKing node, a transmitted frame is retried by the controller and
  the error counters climb to error-passive and bus-off. That is expected, not a
  firmware fault.

## Console (UART0, 115200)
```text
can start [125000|250000|500000|1000000] [normal|listen|selftest]
can status
can send 123#0105FF      11-bit ID, data 01 05 FF
can send 1F334455#11     29-bit ID (exactly 8 hex digits)
can send 123#R           remote request   (123# = empty data frame)
can read                 -> can_rx frame=ID#HEX time_us=N
```
`can read` removes one queued frame; `can_rx=empty` means none. Received frames are
not echoed or forwarded. Starting twice returns `ESP_ERR_INVALID_STATE`.
`can_error=ESP_ERR_NOT_SUPPORTED` means `can send` in listen mode.

## Build and test
```bash
./tools/test_host.sh            # UART framer + CAN codec + config migration
./tools/verify_phase3.sh        # host tests + firmware build (ESP-IDF 5.5.6)
```
Hardware acceptance, with a USB-CAN adapter as the independent node (pyserial and
python-can installed; close other monitors):
```bash
python tools/test_can_fixture.py --console-port CONSOLE_PORT \
    --interface slcan --channel /dev/cu.usbmodemXXXX --baud 500000 \
    --log docs/test-reports/can-500000-hardware.log
```
It verifies: foundation state (CAN stopped, PSRAM), node-to-board standard,
extended, empty, remote and 8-byte frames, board-to-node frames with
`tx_completed` increments, twenty queued frames (sixteen delivered, exactly four
explicit drops), and zero `tx_failed`, `rx_missed`, `rx_overrun`, `bus_off`.
Then do the manual error test: with traffic running, disconnect CANH or the other
node, send frames, watch `can status` (tec rising, `error_passive`, `bus_off`),
reconnect and confirm `recoveries` increments and traffic resumes. Record the
bit rate, termination, adapter and wiring with each log.

## Acceptance state
- [x] TWAI driver module, accept-all filter, counters, bus-off recovery implemented.
- [x] Frame text codec host-tested with sanitizers.
- [x] Driver source syntax-checked against stand-in headers only.
- [ ] Builds against ESP-IDF v5.5.6 (run `./tools/verify_phase3.sh`).
- [ ] Pre-wiring gate completed and recorded.
- [ ] Real-node TX and RX at 500 kbit/s; identifiers and payloads match.
- [ ] Bit rate, termination and test equipment recorded.
- [ ] Error-passive / bus-off / recovery observed.
M3 stays open until the hardware items pass.
