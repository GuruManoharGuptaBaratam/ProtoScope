# Preliminary pinout and connector plan

**Document status:** Phase-0 draft; do not wire from this table yet  
**Module:** ESP32-S3 N16R8 (user-confirmed suffix); carrier revision pending  
**Last updated:** 2026-11-10

## 1. Board-selection status

The user confirmed **N16R8** on 2026-11-10: 16 MB flash and 8 MB Octal
PSRAM. This replaces the original N8R2 candidate. GPIO35/36/37 stay
reserved for octal memory; the existing external UART allocation is GPIO17/18.
The carrier board and revision have not yet been identified.

Before the pin plan is frozen, photograph and record the actual board markings,
module marking, board revision, USB connector arrangement, and RGB LED GPIO.
The initial and v1.1 DevKitC-1 revisions use different RGB LED GPIOs.

## 2. Allocation rules

The preliminary allocation follows these guardrails:

- Keep GPIO19/GPIO20 free for native USB D-/D+.
- Keep GPIO43/GPIO44 free for the board's UART0 programming/debug path.
- Avoid boot-strapping pins GPIO0, GPIO3, GPIO45, and GPIO46.
- Avoid GPIO35/GPIO36/GPIO37 so the design remains compatible with module
  variants that reserve them for octal memory.
- Avoid both possible onboard RGB LED pins GPIO38 and GPIO48 until the physical
  board revision is confirmed.
- Use separate SPI buses for TFT/storage and the external routed SPI port.
- Treat GPIO39-GPIO42 as unavailable to external JTAG while assigned to the
  external SPI port. USB-JTAG/debug strategy must be confirmed in Phase 1.

## 3. Preliminary GPIO allocation

| Function | Signal | GPIO | Direction | Status / note |
|---|---|---:|---|---|
| CAN/TWAI | TX to transceiver D/TXD | 4 | Output | Preliminary |
| CAN/TWAI | RX from transceiver R/RXD | 5 | Input | Preliminary |
| External UART | TX | 17 | Output | Native U1TXD-capable pin |
| External UART | RX | 18 | Input | Native U1RXD-capable pin |
| RS-485 UART | TX to module | 15 | Output | Preliminary; auto-direction module assumed |
| RS-485 UART | RX from module | 16 | Input | Preliminary |
| I2C controller | SDA | 8 | Bidirectional | Pull-up value/module pull-ups TBD |
| I2C controller | SCL | 9 | Output/open-drain | Pull-up value/module pull-ups TBD |
| External SPI controller | SCLK | 39 | Output | Reuses default JTAG pin |
| External SPI controller | MOSI | 40 | Output | Reuses default JTAG pin |
| External SPI controller | MISO | 41 | Input | Reuses default JTAG pin |
| External SPI controller | CS0 | 42 | Output | One configured device initially |
| TFT/SD SPI bus | SCLK | 12 | Output | Shared only by TFT and SD |
| TFT/SD SPI bus | MOSI | 11 | Output | Shared only by TFT and SD |
| TFT/SD SPI bus | MISO | 13 | Input | Required by SD; TFT use depends on module |
| TFT | CS | 10 | Output | Exact module pin labels TBD |
| TFT | D/C | 14 | Output | Exact module pin labels TBD |
| TFT | RESET | 21 | Output | May be optional on some boards |
| microSD | CS | 7 | Output | On-display socket versus separate module TBD |
| Rotary encoder | A | 1 | Input | Debounce/pull-up strategy TBD |
| Rotary encoder | B | 2 | Input | Debounce/pull-up strategy TBD |
| Rotary encoder | Push | 6 | Input | Debounce/pull-up strategy TBD |

GPIO47 remains unallocated as the current spare. The final design should retain
at least one spare GPIO if possible.

## 4. Reserved and avoided pins

| GPIO / signal | Reservation |
|---|---|
| 19 / USB_D- | Native USB; reserved |
| 20 / USB_D+ | Native USB; reserved |
| 43 / U0TXD | Programming/debug console; reserved |
| 44 / U0RXD | Programming/debug console; reserved |
| 0, 3, 45, 46 | Boot/strapping risk; avoid for prototype peripherals |
| 35, 36, 37 | Avoid for cross-variant memory compatibility |
| 38, 48 | Possible onboard RGB LED depending on board revision |
| 47 | Spare |

## 5. Proposed external connector contracts

These are logical signal groups, not frozen connector families or pin numbers.
The final connector drawings must include a keyed orientation and a visible
3.3 V/5 V warning.

| Port | Proposed signals | Electrical note |
|---|---|---|
| UART | GND, 3V3_OUT(optional), TX, RX | 3.3 V logic only |
| I2C | GND, 3V3_OUT(optional), SDA, SCL | Pull-ups present exactly once per designed bus |
| SPI | GND, 3V3_OUT(optional), SCLK, MOSI, MISO, CS0 | Controller/host port; one device initially |
| CAN | CANH, CANL, GND(optional/reference), SHIELD(optional) | Connector and termination control TBD |
| RS-485 | A, B, GND/reference, SHIELD(optional) | A/B naming must be verified against module/device documentation |

DB9 is only a connector shell. ProtoScope's CAN and RS-485 DB9 assignments must
be documented explicitly and must not be described as universal.

## 6. Power/logic verification table

| Item | Supply | Logic | Current | Verification status |
|---|---|---|---|---|
| ESP32-S3 N16R8 | USB/5 V input; onboard 3.3 V regulation | 3.3 V GPIO | TBD under intended workload | Module suffix confirmed; carrier revision pending |
| SN65HVD230 IC | 3.0-3.6 V | 3.3 V compatible | Breakout-dependent/TBD | IC confirmed; breakout pending |
| CAN breakout | TBD from exact schematic | TBD | TBD | Pending exact module |
| Automatic-direction RS-485 module | Vendor claim: broad supply range | Vendor claim: 3.3/5 V compatible | TBD | Must verify exact revision |
| TFT module | TBD | TBD | TBD, including backlight | Pending exact module |
| microSD arrangement | TBD | 3.3 V card signalling; module may add circuitry | Peak write current TBD | Pending exact module/socket |
| Rotary encoder | Passive contacts | Use 3.3 V pull-ups | Negligible excluding pull-ups | Circuit TBD |
| I2C fixture | TBD | Must be 3.3 V compatible or level shifted | TBD | Fixture pending |
| SPI fixture | TBD | Must be 3.3 V compatible or level shifted | TBD | Fixture pending |

## 7. Freeze checks

Do not approve this pinout for wiring until all of the following are complete:

- [ ] Actual ESP32-S3 board and revision photographed/recorded.
- [ ] Exact breakout module pinouts attached to the project documentation.
- [ ] TFT and microSD topology confirmed.
- [ ] All module supply, logic, and current figures recorded from documentation.
- [ ] Development/debug interface choice confirmed.
- [ ] ESP-IDF peripheral mapping checked against the selected board.
- [ ] Power-current budget completed with margin.
- [ ] Connector pin numbers and keyed orientations defined.
- [ ] A peer/manual review finds no duplicate GPIO or unsafe voltage path.

## 8. Reference

The candidate board header and variant information comes from the
[Espressif ESP32-S3-DevKitC-1 v1.1 user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).

