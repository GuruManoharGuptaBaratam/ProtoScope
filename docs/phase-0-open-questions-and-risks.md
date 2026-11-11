# Phase-0 decisions, open questions, and risks

**Last updated:** 2026-11-10

## 1. Decision record

| ID | Decision | Rationale | State |
|---|---|---|---|
| D-001 | Use the revised roadmap's full 20-route coverage target | It is the later, explicit scope commitment | Accepted |
| D-002 | Use I2C controller mode and SPI controller/host mode initially | Makes active, deterministic reads/writes feasible without claiming passive capture | Accepted |
| D-003 | Treat UART -> CAN as the first route | Exercises byte-stream framing, validation, mapping, and framed-bus output with accessible fixtures | Preliminary |
| D-004 | Separate external SPI from TFT/microSD SPI | Reduces contention and preserves deterministic external route tests | Preliminary pending GPIO/board verification |
| D-005 | Use boot-relative monotonic log time initially | Avoids inventing a real-time-clock capability | Accepted |
| D-006 | Keep USB limited to power/programming | Matches confirmed project scope | Accepted |
| D-007 | Keep the first prototype USB-powered | Avoids premature battery/power-path design | Accepted |
| D-008 | Use the user-confirmed ESP32-S3 N16R8 module | Replaces the handover N8R2 assumption; 16 MB flash / 8 MB Octal PSRAM | Suffix confirmed 2026-11-10; full model/carrier revision pending |

## 2. Questions that block Phase-0 completion

### Controller board

- **Q-001:** What exact text is printed on the ESP32-S3 module and development
  board?
- **Q-002:** Is the board the initial DevKitC-1, v1.1, or a third-party
  DevKitC-1-compatible revision?
- **Q-003:** Which physical USB connector is used for programming, and is there a
  separate native USB connector?
- **Q-004 resolved:** User reports N16R8; firmware memory configuration updated.
  Complete Q-001/Q-002 with the full model and carrier revision.

### Communication modules

- **Q-010:** What is the exact CAN breakout manufacturer/SKU and schematic?
- **Q-011:** Does the CAN breakout contain fixed or switchable 120-ohm
  termination, and how is the SN65HVD230 `RS` pin configured?
- **Q-012:** What exact IC/revision is on the automatic-direction RS-485 board?
- **Q-013:** What are the RS-485 board's verified UART-side pinout, logic
  thresholds, direction behavior, termination, and bias network?

### Display and storage

- **Q-020:** What is the exact TFT product/module and pinout?
- **Q-021:** Does it have a usable microSD socket, and does that socket share the
  TFT SPI lines?
- **Q-022:** What are the TFT logic voltage and backlight current?
- **Q-023:** Is a separate SD module required?

### Test fixtures and route definitions

- **Q-030:** Which UART, CAN, and RS-485 adapters/nodes are available?
- **Q-031:** Which programmable MCU board will implement deterministic I2C and
  SPI peripheral fixtures?
- **Q-032:** Will Modbus RTU be the first semantic RS-485 profile, or should all
  initial RS-485 route tests use a project-defined raw frame?
- **Q-033:** Which real device profiles, register maps, CAN IDs, units, scaling,
  signedness, and byte order will replace the remaining route-matrix `TBD`s?
- **Q-034:** Phase 7 requires all 20 route tests to pass, while later roadmap
  wording sometimes refers only to the "selected route(s)" during full
  integration and PCB validation. Should Phases 10 and 11 repeat the full
  20-route regression on the integrated hardware, or use a documented
  representative hardware subset while retaining full route-level test
  evidence from Phase 7?

### Mechanical and connectors

- **Q-040:** Will CAN and RS-485 use DB9 in the prototype, or screw terminals
  with DB9 deferred to the enclosure/PCB?
- **Q-041:** Which keyed connector family and pitch should UART/I2C/SPI use?

## 3. Risk register

| Risk | Likelihood | Impact | Current mitigation | Closure evidence |
|---|---|---|---|---|
| Mismatched ESP32-S3 board variant | Medium | High | N16R8 suffix confirmed; memory configuration corrected; record full carrier revision | Recorded board/module/revision, successful PSRAM boot and approved pinout |
| GPIO conflict or hidden onboard use | Medium | High | Reserve USB/UART0/strapping/memory/RGB pins; keep spare GPIO | Pin-plan review on exact board plus Phase-1 bring-up |
| 5 V logic damages ESP32-S3 | Medium | Critical | Require separate supply/logic verification; no direct 5 V GPIO | Datasheet-backed voltage table and wiring review |
| TFT/SD/external SPI contention | Medium | High | Separate external SPI bus provisionally; unique CS lines | Concurrent display, SD, and external SPI test |
| CAN termination/transceiver wiring error | Medium | High | Verify exact breakout schematic and topology | Documented CAN fixture and successful TX/RX |
| RS-485 auto-direction fails at selected framing/rate | Medium | High | Characterize actual module; keep module behavior separate from protocol | Measured half-duplex tests at supported settings |
| I2C/SPI route semantics remain underspecified | High | High | Use controller reads/writes and programmable fixtures | Defined transaction profile and passing both directions |
| Generic event structure loses metadata | Medium | High | Require protocol-specific metadata and transaction boundaries | Phase-6 unit vectors for all interfaces |
| Slow SD/TFT work loses messages | Medium | High | Queue/buffer, non-blocking UI, drop counters | Load test comparing traffic and recorded counts |
| Attempting 20 routes simultaneously | Medium | High | Build in representative batches and regress all routes | Batch test reports and full regression report |
| Route loop or duplicate re-entry | Medium | High | Event provenance/hop metadata and explicit loop rejection | Loop-injection tests |
| Power regulator margin is insufficient | Medium | High | Measure/obtain current requirements before integration | Calculated budget plus integrated voltage/current test |
| DB9 pinout assumption connects incompatible equipment | Low-Medium | High | Define ProtoScope-specific labels and keyed documentation | Frozen connector drawing and continuity test |

## 4. Decisions intentionally deferred

- Custom PCB schematic/layout.
- Enclosure dimensions and connector placement.
- Internal battery, charger, or power-path design.
- A user-facing PC application or USB protocol bridge.
- Calendar-time logging/RTC.
- I2C/SPI peripheral mode or passive bus-sniffing claims.
- CAN FD support.
- Exact semantic conversions for routes whose device definitions are not yet
  selected.

