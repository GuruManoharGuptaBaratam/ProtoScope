# ProtoScope

ProtoScope is a portable ESP32-S3-based gateway for connecting, converting,
monitoring, and logging traffic across five interfaces:

- UART
- CAN
- RS-485
- I2C
- SPI

The conversion target is a configurable matrix of all 20 directed routes
between different interfaces. A listed route is not automatically usable: it
requires a defined source format, mapping, destination format, compatible bus
roles, and repeatable test evidence.

## Current status

**Phase 3 — CAN implementation ready for hardware testing (Phase 2 UART below it).**

The user confirmed the board's **N16R8** module suffix: **16 MB flash and
8 MB Octal PSRAM**. The former N8R2 defaults caused the reported Quad PSRAM
boot failure. Follow [PSRAM recovery and the post-flash checklist](docs/psram-recovery-and-post-flash.md)
before testing UART.

Phase-0 requirements and architecture are under [`docs/`](docs/phase-0-status.md).
The Phase-1 ESP-IDF application, modular components, safe GPIO initialization,
FreeRTOS runtime, console, build configuration, and verification plan are
described in [`docs/phase-1-foundation.md`](docs/phase-1-foundation.md).

UART1 is available on GPIO17/18 and stays stopped until `uart start` is entered
in the development console. [Phase 2 instructions](docs/phase-2-uart.md) cover
framing, console commands, wiring and automated external-adapter tests.
Successful physical boot and external UART TX/RX are still unverified.

**Phases 5-6 (I2C/SPI controllers, common events):** see
[`docs/phase-5-i2c-spi.md`](docs/phase-5-i2c-spi.md) and
[`docs/phase-6-event-abstraction.md`](docs/phase-6-event-abstraction.md). Use
`./tools/verify_phase6.sh`. Not built against ESP-IDF or tested on hardware yet.
There is no routing or conversion yet (Phase 7).

**Phase 4 (RS-485):** raw half-duplex serial on UART2 (GPIO15/16) through an
automatic-direction module; stopped until `rs485 start`. See
[`docs/phase-4-rs485.md`](docs/phase-4-rs485.md). Use `./tools/verify_phase4.sh`.
Not built against ESP-IDF or tested on hardware yet. Modbus RTU is not included.

**Phase 3 (CAN):** TWAI on GPIO4/5 through the SN65HVD230 transceiver; stopped
until `can start` in the console. See [`docs/phase-3-can.md`](docs/phase-3-can.md)
for the pre-wiring gate, commands and the hardware acceptance steps. Use
`./tools/verify_phase3.sh` for host tests plus the firmware build. It has not
been built against ESP-IDF or tested on hardware yet.

**VS Code / path with spaces:** see [`docs/vscode-and-idf-setup.md`](docs/vscode-and-idf-setup.md).
`./tools/idf_phase1.sh` builds from a space-free mirror automatically.

## Phase 1 quick start

With ESP-IDF **v5.5.6** and its tools installed, run:

```bash
cd "$HOME/esp/ProtoScope"
python3 tools/configure_n16r8.py
./tools/verify_phase1.sh
./tools/idf_phase1.sh -p PORT flash monitor
```

Replace `PORT` with the board's serial port. The project launcher selects the
5.5.6 SDK, its matching Python environment, and ESP32-S3 even if the current
terminal has another SDK active. It accepts the same arguments as `idf.py` and
defaults to `build` when no arguments are given.

ESP-IDF 5.4 does not satisfy the project's `>=5.5,<6.0` requirement; 6.x is also
outside that range. Sourcing `export.sh` activates an installed version; it does
not upgrade it. See the [workstation setup and error recovery guide](docs/phase-1-foundation.md#3-first-time-workstation-setup)
for installation, copying this workspace to a path without spaces, and VS Code
version selection. `sdkconfig.defaults` selects `esp32s3` for fresh builds, and
`version.txt` supplies the firmware version when building without Git metadata.

For an existing build copy, run `python3 tools/configure_n16r8.py` before
building. It backs up and updates the stale memory settings in `sdkconfig`.
Use `./tools/verify_phase2.sh` for the UART host tests plus firmware build.
The module suffix is confirmed; full carrier-board identification and header
verification remain necessary before wiring external fixtures.

## Current scope guardrails

- USB-C is for power and programming. A user-facing PC bridge/application is
  future scope.
- I2C and SPI initially operate in controller/host mode. A source route means a
  configured read/poll; a destination route means a configured write.
- CAN uses the ESP32-S3 TWAI controller with an external transceiver.
- RS-485 is half duplex; RS-485 operation does not by itself imply Modbus RTU.
- Same-interface forwarding is outside the 20-route count.
- A custom PCB and enclosure wait until the wired prototype passes integration
  testing.

