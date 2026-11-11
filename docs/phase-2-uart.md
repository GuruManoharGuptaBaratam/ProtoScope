# Phase 2 — UART interface

**Milestone:** M2 — independent external UART TX/RX  
**Firmware:** 0.2.0  
**State:** implementation and software tests passed; hardware acceptance pending

The revised roadmap defines Phase 2 as UART alone. UART0 stays dedicated to
the development console. UART1 uses the existing GPIO17 TX / GPIO18 RX
allocation. No CAN, RS-485, I2C, SPI or conversion route is started.

## Before wiring

First pass [N16R8 boot recovery](psram-recovery-and-post-flash.md): 16 MB flash,
8388608 bytes of PSRAM, memory-test success and a responsive console.
The N16R8 suffix is user-confirmed; the carrier model/revision and exact
adapter remain to be recorded. Verify the actual board headers expose GPIO17
and GPIO18 and that the adapter's TX/RX logic is **3.3 V**.

With power disconnected, connect a documented 3.3 V USB-to-UART adapter:

| ESP32-S3 header | External adapter |
|---|---|
| GPIO17 / UART1 TX | RX |
| GPIO18 / UART1 RX | TX |
| GND | GND |

Leave the adapter's VCC/5V/3V3 power pin disconnected; the ESP board uses its
programming USB power. Do not use RS-232 voltage levels or connect RS-485 A/B
to these pins. The external adapter appears as a second serial port; it is
separate from the board's programming/console port.

For a preliminary board-only loopback, use one jumper GPIO17 → GPIO18 with
no external adapter attached. Power off before fitting/removing it. Loopback
is a useful check but does not replace the external-device acceptance test.

## Build and manual test

From the updated no-space build copy:

```bash
python3 tools/configure_n16r8.py
./tools/verify_phase2.sh
./tools/idf_phase1.sh -p PORT flash monitor
```

The build launcher keeps its existing `idf_phase1.sh` name for compatibility
and selects ESP-IDF v5.5.6 for both milestones. `verify_phase2.sh` runs the
host framing/memory-migration tests and builds the firmware. Host tests need
a C compiler with AddressSanitizer/UndefinedBehaviorSanitizer and Python 3.

At the `protoscope>` prompt:

```text
status
uart status
uart start 115200
uart send 50494E470A
uart read
uart status
```

At boot, `uart_active=no`. `uart start` selects 115200 8N1 by default;
`uart start 9600` selects the other supported test rate. Flow control is off.
Starting an already active port returns `ESP_ERR_INVALID_STATE`; reboot to
stop it or change baud. Start only after checking the wiring.

`uart send 50494E470A` sends exactly `PING\n`, with no extra bytes. The
external adapter should receive `50 49 4E 47 0A`. With the preliminary loopback
jumper, `uart read` should report:

```text
uart_rx bytes=5 hex=50494E470A time_us=<monotonic timestamp>
```

With an external adapter, send `HELLO\n` from its terminal, then run `uart read`
on the board console. Expect `48454C4C4F0A`. Set the external terminal to the
same baud/8N1 and an LF or CRLF line ending. `uart read` removes one queued
frame and prints its exact wire bytes; `uart_rx=empty` means no complete frame
is available. Received bytes are not automatically echoed or forwarded.

`uart send` accepts a contiguous even-length hex string encoding 1–64 raw
bytes; the firmware adds no delimiter. The configuration structure carries
baud/data/parity/stop settings, but Phase 2 intentionally validates only
9600/115200 with 8N1. Other configurations are rejected.

## Test framing contract

The initial UART receive test uses printable ASCII lines, compatible with the
outer framing planned for RT-01. It does not parse CAN fields or enable RT-01.

- A frame ends at LF (`0A`); a single CR immediately before LF is allowed.
- Payload must contain at least one printable ASCII byte (`20`–`7E`).
- The limit is **64 total wire bytes including CR/LF**: up to 63 payload bytes
  with LF, or 62 with CRLF. Raw bytes, including delimiters, are retained.
- Empty lines, embedded CR, NUL, other control bytes and non-ASCII input are
  malformed. Overlong lines have their own counter. Reject once and discard
  through the next LF; a 200 ms idle interval also restores synchronization.
- An incomplete otherwise valid frame expires after 200 ms since the last
  processed byte. Count it once; following input starts a new frame.
- Consecutive lines and arbitrary read chunk boundaries are supported.

This text framing is a Phase-2 test profile. Binary framing and device-specific
protocol decoders belong to later explicitly defined profiles.

## Runtime and counters

`components/proto_uart` owns UART1, a 2048-byte driver RX ring, a 32-event
driver queue, a dedicated priority-6/4096-byte-stack RX task, and a 16-frame
application queue. It uses the XTAL UART clock while the foundation changes
CPU frequency. The RX task never waits on console output.

When the frame queue is full, drop the newest complete frame and increment
`rx_queue_drops`. `rx_frames` counts valid assembled frames including those
dropped by this queue. `rx_bytes` counts bytes actually drained by the task;
it cannot include bytes already lost in hardware.

`malformed`, `overlong`, `partial_timeouts`, FIFO/ring overflow, parity/frame
error and break counters expose rejection and driver-reported errors. A
FIFO/ring overflow flushes pending RX data and forces delimiter/idle
resynchronization. ESP-IDF does not expose a count of dropped UART event
notifications; under overload these error-event counts are not guaranteed
complete. The worker also polls buffered data when no event arrives.

TX is serialized with a 500 ms lock wait and a separate 500 ms transmission
deadline. It fills the hardware FIFO without an unbounded software TX queue.
`tx_bytes` counts bytes accepted into that FIFO; `tx_completed` increments
only after the driver reports TX done. This does not prove peer receipt.
A timeout may leave a transmitted prefix, so the caller must not blindly
retry. Disconnected RX/TX cannot be inferred from an unacknowledged UART TX.

Timeout timestamps are task-observed, not per-byte hardware timestamps.
The bounded tests below establish the initial operating envelope; continuous
saturation and future display/storage contention need later load testing.

## Automated external-adapter test

Close other monitors/terminals on both ports. Use a Python environment with
`pyserial` installed (the ESP-IDF Python environment provides it). The script
reboots the board before each run and saves the transcript:

```bash
python tools/test_uart_fixture.py \
    --console-port CONSOLE_PORT --uart-port ADAPTER_PORT --baud 115200 \
    --log docs/test-reports/uart-115200-hardware.log
python tools/test_uart_fixture.py \
    --console-port CONSOLE_PORT --uart-port ADAPTER_PORT --baud 9600 \
    --log docs/test-reports/uart-9600-hardware.log
```

The two ports must be different physical devices. Each run verifies:

1. PSRAM initialized at 8 MB and external UART stopped after reboot.
2. Fragmented CRLF, consecutive LF frames and the 64-byte boundary on RX.
3. Exact 5-byte and 64-byte TX observed by the adapter.
4. Overlong, malformed and partial-timeout rejection; subsequent recovery.
5. Twenty queued frames: sixteen delivered, exactly four explicit drops.
6. One hundred numbered exchanges in both directions with byte comparison.
7. No unexpected FIFO overflow, driver error or TX failure.

Repeat the foundation's five boots and 30-minute soak. Record the full board
model, adapter SKU, logic level, wiring and test date alongside each log.
Wrong baud/parity and disconnection tests must be observed separately: UART
cannot guarantee a detectable error for every physical mismatch, and TX done
does not imply a connected peer.

## Acceptance state

- [x] Module, configuration structure, raw TX and buffered RX implemented.
- [x] Framing and recovery tested on host with memory/undefined-behavior sanitizers.
- [x] N16R8 migration tested; firmware builds with ESP-IDF v5.5.6.
- [x] Reproducible external-adapter test program supplied.
- [ ] N16R8 physical boot, PSRAM memory test and foundation soak passed.
- [ ] Carrier headers and exact 3.3 V adapter verified and recorded.
- [ ] External TX/RX tests passed at both supported rates, logs saved.

M1/M2 hardware acceptance remains open. No connected ESP serial port was
available during this implementation.
