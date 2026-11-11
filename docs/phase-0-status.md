# Phase-0 status

**Milestone:** M0 — Requirements frozen  
**Current state:** Complete — requirements/system-design baseline accepted  
**Last updated:** 2026-11-10

The roadmap is numbered Phase 0 through Phase 11: one foundation phase plus
eleven subsequent development phases.

## Deliverables

| Deliverable | File | Status |
|---|---|---|
| Project requirements | [`project-requirements.md`](project-requirements.md) | Baseline created |
| Hardware/software block diagram | [`system-architecture.md`](system-architecture.md) | Baseline created |
| Preliminary GPIO/pin allocation | [`pinout-and-connectors.md`](pinout-and-connectors.md) | Draft; hardware verification required |
| Interface operating-mode table | [`system-architecture.md`](system-architecture.md#3-interface-operating-modes) | Created |
| Preliminary BOM | [`bom.md`](bom.md) | Created; exact SKUs/current figures pending |
| 20-route conversion matrix | [`conversion-mappings/route-matrix.md`](conversion-mappings/route-matrix.md) | All routes listed; RT-01 specified, remaining device formats TBD |
| First route implementation/test order | [`conversion-mappings/route-matrix.md`](conversion-mappings/route-matrix.md#4-staged-implementation-order) | Created |
| Unresolved questions and risks | [`phase-0-open-questions-and-risks.md`](phase-0-open-questions-and-risks.md) | Created |

## Task checklist

- [x] Confirm UART, CAN, RS-485, I2C, and SPI scope.
- [ ] **Partial:** identify ESP32-S3 board candidate: N16R8 suffix confirmed by user on 2026-11-10; full
  board model/revision not verified.
- [x] List planned modules and peripherals.
- [ ] Record documentation-backed supply, logic, and current requirements for
  every exact module.
- [x] Create a conflict-aware preliminary GPIO allocation.
- [x] Define initial operating modes, including I2C/SPI controller roles.
- [x] Define all 10 pairs and 20 directed routes.
- [x] Create the route matrix and mark unknown device definitions `TBD`.
- [ ] **Partial:** define test-fixture strategy; exact equipment remains pending.
- [x] Define staged route implementation order.
- [x] Define minimum logging and TFT information.
- [x] Create a requirements/decision/risk record.

## Exit-criteria review

| Exit criterion | State | Remaining work |
|---|---|---|
| Every interface has an initial operating mode | Met | Reconfirm against exact fixtures |
| Exact ESP32-S3 board candidate identified | Partial | Record full module/carrier revision; N16R8 suffix confirmed |
| Power/logic requirements known or pending explicitly | Partial | Pending items are explicit; exact values/current budget still needed |
| Preliminary pin plan has no obvious conflicts | Met for candidate | Validate on physical board/module revisions |
| TX/RX behavior and I2C/SPI roles explicit | Met | None for baseline |
| 20-route matrix exists | Met | Most device-level formats intentionally TBD |
| First route fully specified | Met at protocol level | Exact UART/CAN fixtures and termination still need selection |
| Remaining routes have implementation/test sequence | Met | Device profiles remain to be selected |
| Architecture can be built incrementally | Met on paper | Must be validated during Phases 1-7 |

## Phase-0 closure gate

M0 is accepted as the requirements and system-design baseline. The following
physical verification items are carried into the Phase-1 pre-wiring gate and
must still be resolved before external modules are connected:

1. Record the actual ESP32-S3 board/module/revision.
2. Record exact CAN, RS-485, TFT, microSD, and encoder modules.
3. Attach or link each pinout/datasheet and fill supply, logic, and current data.
4. Select the UART/CAN/RS-485 adapters and programmable I2C/SPI fixture.
5. Re-run the pin/power review and freeze the connector plan.

These items do not block the board-only Phase-1 firmware foundation, but they do
block peripheral wiring and Phase-2 interface bring-up.

