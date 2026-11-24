# Phase 8 — Communication monitor and microSD logger

**Milestone:** M8 — selected events displayed and logged with tested error handling  
**Firmware:** 0.9.0  
**State:** implemented and host-tested with a mock card; build and card hardware tests pending

## What it does
- **Monitor** (`monitor` console command, also feeds the Phase 9 OLED): one snapshot of each interface
  (started?, received, sent, errors, drops, short setting text), routes (enabled/ready/sent/rejected),
  the logger (state, file, records, drops, write errors) and the system (uptime, heap, PSRAM).
- **Logger** (`log` command): writes events to the microSD card as CSV, buffered so a slow card never
  holds up a communication task.

## What gets logged
Every event that passes through the common event contract (Phase 6): received UART/RS-485/CAN events,
transmit results, and I2C/SPI request and result events. That covers the routing engine, the `msg`
commands and monitor mode. Raw console commands such as `uart read` or `can read` bypass the contract
and are not logged. Use `log filter uart can ...` to choose protocols (default all).
`log monitor on` additionally reads and logs UART/RS-485/CAN events that **no enabled route uses**; this
takes those events away from the console `read` commands, so it is off by default. Nothing is queued
until `log start` is typed.

## File format (schema 1)
Files are `LOG0001.CSV`, `LOG0002.CSV`, ... on a FAT32 card (8.3 names). Each run starts a new file and
never overwrites an existing one. A file rotates at 4 MB; every file starts with its own header:
```text
# ProtoScope log schema=1 firmware=0.9.0
# timestamp_us = microseconds since boot (no calendar clock)
timestamp_us,event_id,correlation_id,protocol,direction,status,error_code,hop,origin_route,length,data_hex,meta
1001,1,0,CAN,RX,OK,0,0,0,3,0105FF,id=123;ext=0;rtr=0;dlc=3
```
`meta` keeps the protocol details with `;` separators so a row never needs quoting: serial `baud;term`,
CAN `id;ext;rtr;dlc`, I2C `addr;op;wr;rd;ack`, SPI `cs;mode;clock_hz;len`. Time is microseconds since
boot (FR-033): there is no real-time clock, and a reboot restarts the count (each file is one boot).

## Failure handling (all tested with a mock card)
| Situation | Behaviour | Where you see it |
|---|---|---|
| Card missing or unreadable | state `no_card`; events dropped and counted; retries every 5 s with a new file | `log status`, OLED "!SD NO CARD" |
| Card full / file numbers used up | state `full`; events dropped and counted; retries every 30 s | `log status`, OLED "!SD FULL" |
| Write or flush error, card pulled | state `error`; the file is closed, remount tried after 5 s | `write_errors`, OLED "!SD ERROR" |
| Writer too slow | the newest events are dropped and counted (`dropped_total`, `queue` high-water) | `log status` |
| Power loss | data is flushed every 32 records or 1 s, so at most about a second is lost | - |
The card is never formatted. Insert it formatted as FAT32.

## Console
```text
monitor
log start | log stop | log status | log filter all|uart|rs485|can|i2c|spi ... | log monitor on|off
```

## Wiring (see docs/hardware/ProtoScope_Phase1-9_netlist.md)
microSD module (3.3 V SPI): SCK to GPIO12, MOSI to GPIO11, MISO to GPIO13, CS to GPIO7, VCC to 3V3, GND to GND.
Power the module from 3V3 unless it has a 5 V regulator and level shifter; then check MISO idles at 3.3 V.

## Tests
- Host (`./tools/test_host.sh`): CSV format and file-name parsing, queue overflow, normal run, flushing,
  rotation (every file has its header), no card then card inserted, write and flush failures with
  recovery, full card, exhausted file numbers, filter, and drop counters.
- Hardware, no extra devices: SPI loopback jumper (GPIO40-GPIO41) plus a card:
  `python tools/test_phase8_9_fixture.py --console-port PORT --log docs/test-reports/phase8-9.log`
  (add `--card-dir /Volumes/YOURCARD` after re-inserting the card to check the newest file; `--skip-ui`
  to run only Phase 8).
- Manual: pull the card while logging (expect `error`/`no_card`, a new file after re-inserting); run with no card.

## Not covered
USB log export (remove the card instead), calendar timestamps, file formats other than CSV, and logging
console-only reads. Throughput limits are not measured.

## Acceptance state
- [x] Monitor snapshot and console command; logger with queue, rotation, flush policy and counters.
- [x] No-card, full-card and write-failure behaviour, tested on a mock card.
- [ ] Builds against ESP-IDF v5.5.6 (`./tools/verify_phase9.sh`).
- [ ] A real card logs during communication; the file reads correctly on a computer; counts match.
- [ ] Card pulled and re-inserted while logging recovers.
M8 closes when the hardware items pass.
