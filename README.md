# ProtoScope

ProtoScope is an ESP32-S3-based portable hardware gateway designed for connecting, converting, monitoring, and logging traffic across five industrial and embedded serial protocols:

- **UART** (external asynchronous serial with stream framing)
- **CAN** (Classic CAN 2.0A/B via ESP32-S3 TWAI controller)
- **RS-485** (half-duplex differential serial)
- **I2C** (controller mode transactions)
- **SPI** (controller mode bus transactions)

The conversion target is a configurable matrix of all **20 directed routes** between disparate protocols (e.g. UART ↔ CAN, RS-485 ↔ I2C, SPI ↔ CAN). ProtoScope features non-blocking queues, an isolated common event bus, on-device microSD CSV event logging, real-time bus monitoring, and a standalone 0.96-inch OLED + rotary encoder local UI.

---

## Current Status

**Firmware Version:** `0.9.1`  
**Target Hardware:** ESP32-S3-WROOM-1 / N16R8 (16 MB Octal Flash, 8 MB Octal PSRAM)  
**Implementation State:** Phases 0 through 9 are fully implemented in software, with full host test suite passes under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan). Board-level hardware acceptance testing is pending.

### Milestone & Roadmap Progress

| Phase | Milestone | Description | Component(s) | Status |
|:---|:---|:---|:---|:---:|
| **Phase 0** | **M0** | Requirements, preliminary pinout, BOM, 20-route matrix, architecture | `docs/` | Complete |
| **Phase 1** | **M1** | ESP32-S3 N16R8 foundation, safe GPIO parking, FreeRTOS tasks, console REPL | `proto_board`, `proto_platform`, `proto_tasks`, `proto_console` | Complete |
| **Phase 2** | **M2** | External UART1 (GPIO17/18) driver, stream framing engine, host tests | `proto_uart` | Complete |
| **Phase 3** | **M3** | Classic CAN (TWAI on GPIO4/5 + SN65HVD230 transceiver), frame codec | `proto_twai` | Complete |
| **Phase 4** | **M4** | Half-duplex RS-485 on UART2 (GPIO15/16) with echo-suppression & timing | `proto_rs485` | Complete |
| **Phase 5** | **M5** | External I2C controller (GPIO8/9) and SPI controller (GPIO39-42) | `proto_i2c`, `proto_spi` | Complete |
| **Phase 6** | **M6** | Common event envelope (`proto_event`), metadata codec, driver contracts | `proto_event` | Complete |
| **Phase 7** | **M7** | 20-route translation engine (`proto_route`), policies, loop guard | `proto_route` | Complete |
| **Phase 8** | **M8** | Live communication monitor & microSD FAT32 CSV logger with 4 MB rotation | `proto_monitor`, `proto_storage` | Complete |
| **Phase 9** | **M9** | 0.96" SSD1306 I2C OLED display (`proto_uibus`) & EC11 rotary encoder UI | `proto_uibus`, `proto_ui` | Complete |

---

## System Architecture

```text
                                  +-----------------------------+
                                  |     Interactive Console     |
                                  |     (UART0 - 115200 8N1)    |
                                  +--------------+--------------+
                                                 |
+------------------------------------------------v-----------------------------------------------+
|                                       ProtoScope Core Engine                                   |
|                                                                                               |
|  +--------------------+        +---------------------+        +----------------------------+  |
|  |   proto_monitor    | <----> |     proto_route     | <----> |       proto_storage        |  |
|  |  (Live Bus Stats)  |        |  (20-Route Engine)  |        |   (MicroSD CSV Logger)     |  |
|  +---------+----------+        +----------+----------+        +--------------+-------------+  |
|            |                              |                                  |                |
|            |                 +------------v------------+                     |                |
|            +---------------> |       proto_event       | <-------------------+                |
|                              |  (Common Event Envelope)|                                      |
|                              +------------+------------+                                      |
|                                           |                                                   |
|         +-----------------+---------------+-----------------+------------------+              |
|         |                 |               |                 |                  |              |
|  +------v-------+  +------v------+  +-----v------+   +------v-------+   +------v-------+      |
|  |  proto_uart  |  | proto_twai  |  |proto_rs485 |   |  proto_i2c   |   |  proto_spi   |      |
|  |  (UART1 TX/RX|  | (Classic CAN|  | (UART2 TX/ |   |  (Controller |   |  (Controller |      |
|  |   GPIO17/18) |  |   GPIO4/5)  |  | RX GPIO15/ |   |   GPIO8/9)   |   |   GPIO39-42) |      |
|  +--------------+  +-------------+  |    16)     |   +--------------+   +--------------+      |
|                                     +------------+                                            |
|                                                                                               |
|  +-----------------------------------------------------------------------------------------+  |
|  | Local Hardware UI: proto_uibus (OLED I2C GPIO14/10) + proto_ui (SSD1306 + EC11 Encoder) |  |
|  +-----------------------------------------------------------------------------------------+  |
|  | Platform: proto_board (Safe GPIO) | proto_platform (NVS/Heap) | proto_tasks (FreeRTOS)  |  |
+--+-----------------------------------------------------------------------------------------+--+
```

### Component Structure

```text
components/
├── proto_board/       Safe GPIO parking table and conflict validation
├── proto_platform/    NVS initialization, CPU/power management, heap/PSRAM diagnostics
├── proto_tasks/       FreeRTOS supervisor task and worker task queues
├── proto_console/     Interactive UART0 development REPL commands
├── proto_uart/        External UART1 driver with stream framing engine
├── proto_twai/        Classic CAN 2.0A/B driver on ESP32-S3 TWAI controller
├── proto_rs485/       Half-duplex RS-485 driver on UART2 with timing control
├── proto_i2c/         External I2C controller driver
├── proto_spi/         External SPI controller driver
├── proto_event/       Unified event model, metadata encoding, driver abstraction
├── proto_route/       20-route conversion matrix and routing engine
├── proto_monitor/     System-wide traffic statistics and snapshot aggregator
├── proto_storage/     MicroSD card SPI driver, non-blocking queue, and CSV logger
├── proto_uibus/       Dedicated internal I2C bus for display hardware
└── proto_ui/          SSD1306 OLED driver, 1bpp canvas, bitmap font, EC11 encoder UI
```

---

## Hardware Pin Allocation

ProtoScope follows a single source of truth for all GPIO mappings defined in [`proto_board.h`](components/proto_board/include/proto_board.h) and detailed in [`ProtoScope_Phase1-9_netlist.md`](docs/hardware/ProtoScope_Phase1-9_netlist.md):

| Interface | Signal | GPIO | Header Pin | Notes |
|:---|:---|:---:|:---:|:---|
| **Console** | TXD0 / RXD0 | GPIO43 / GPIO44 | Onboard USB | Primary programming & interactive console REPL (115200 8N1) |
| **External UART** | UART1 TX / RX | GPIO17 / GPIO18 | J1-10 / J1-11 | 3.3 V logic levels; framing engine supports arbitrary baud rates |
| **CAN (TWAI)** | TXD / RXD | GPIO4 / GPIO5 | J1-4 / J1-5 | Connects to SN65HVD230 transceiver; 120 Ω termination at bus ends |
| **RS-485** | TXD / RXD | GPIO15 / GPIO16 | J1-8 / J1-9 | Connects to auto-direction transceiver module |
| **External I2C** | SDA / SCL | GPIO8 / GPIO9 | J1-12 / J1-15 | Controller mode; external 4.7 kΩ pull-ups if needed |
| **External SPI** | SCLK / MOSI / MISO / CS | GPIO39 / 40 / 41 / 42 | J3-9 / 8 / 7 / 6 | Controller mode; supports SPI modes 0-3 |
| **Internal UI Bus**| OLED SDA / SCL | GPIO14 / GPIO10 | J1-20 / J1-16 | Dedicated I2C bus for SSD1306 0.96" OLED display |
| **MicroSD SPI** | MOSI / SCLK / MISO / CS | GPIO11 / 12 / 13 / 7 | J1-17 / 18 / 19 / 7 | FAT32 formatted card; automated CSV rotation at 4 MB |
| **Rotary Encoder** | A / B / Push Switch | GPIO1 / GPIO2 / GPIO6 | J3-4 / J3-5 / J1-6 | EC11 encoder with push button (uses internal ESP32 pull-ups) |

---

## Testing & Verification

### 1. Host Tests (No Hardware Required)

The project includes unit and integration tests executable directly on host machines with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
./tools/test_host.sh
```

**Coverage includes:**
- UART stream framing, fragmentation, boundary conditions, and error recovery
- CAN frame codec, bitrates, 11-bit / 29-bit IDs, remote frames
- RS-485 guard times and character timing validation
- I2C & SPI request/response codec and transaction verification
- Common event envelope validation, metadata serialization, and round-trips
- Route engine table, loop prevention, and 20 directed conversions
- MicroSD CSV schema, queue buffering, rotation, and mock error handling
- UI canvas drawing, font rendering, encoder state decoding, and menu navigation
- N16R8 Octal PSRAM/Flash configuration checks

### 2. Automated Fixture Tests

```bash
python3 tools/test_uart_fixture.py      # UART fixture tests
python3 tools/test_can_fixture.py       # CAN fixture tests
python3 tools/test_phase8_9_fixture.py  # Monitor, logger, and UI fixture tests
```

### 3. Firmware Build Verification

ESP-IDF v5.5.6 is required for firmware compilation:

```bash
./tools/verify_phase9.sh
```

*(Note: If working in a directory path containing spaces, use `./tools/idf_phase1.sh build` which automatically mirrors the source to a space-free build directory).*

---

## Interactive Console Reference

ProtoScope features an interactive REPL on UART0 (115200 8N1). Key commands include:

| Command | Description | Example |
|:---|:---|:---|
| `help` | List all available console commands | `help` |
| `status` | Display full system status (platform, runtime, all interfaces) | `status` |
| `heap` | Print current and minimum free heap and PSRAM | `heap` |
| `uart` | Start, query, send, or read external UART1 | `uart start 115200`, `uart send 48454C4C4F0A` |
| `can` | Start, query, send, or read Classic CAN | `can start 500000 normal`, `can send 123#01020304` |
| `rs485` | Start, query, send, or read half-duplex RS-485 | `rs485 start 9600`, `rs485 send AABBCC` |
| `i2c` | Configure or execute I2C controller transactions | `i2c start 100000`, `i2c tx 68 75` |
| `spi` | Configure or execute SPI controller transactions | `spi start 1000000 0`, `spi xfer 9F000000` |
| `msg` | Transmit or inspect events via the unified event contract | `msg send can 123#AABB`, `msg read` |
| `route` | Configure, enable, disable, and monitor the 20 conversion routes | `route enable rt-01`, `route status` |
| `monitor` | Display real-time aggregated snapshot of all interfaces & routes | `monitor` |
| `log` | Control microSD card CSV logging and filtering | `log start`, `log status`, `log filter uart can` |
| `ui` / `display` | Inspect or control local OLED and rotary encoder state | `ui status`, `ui screen home`, `ui push` |

---

## Current Scope Guardrails

- **USB-C Interface:** Dedicated to power, firmware programming, and the development console.
- **I2C & SPI Modes:** Operate strictly in controller (master) mode.
- **CAN Controller:** Employs the ESP32-S3 internal TWAI controller with an external 3.3 V transceiver (SN65HVD230). Classic CAN only (no CAN FD).
- **RS-485 Mode:** Half-duplex operation with automated direction control.
- **Conversion Routes:** Strict validation against route loops and unmapped data schemas.
- **Display & Encoder:** Runs in an independent low-priority task, ensuring UI rendering never interrupts or delays real-time communications.
