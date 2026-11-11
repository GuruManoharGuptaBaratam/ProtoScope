# Project requirements

**Document status:** Phase-0 baseline, revision 0.1  
**Phase status:** Complete — baseline accepted; electrical checks remain wiring gates  
**Last updated:** 2026-11-10

## 1. Source context and precedence

This baseline was prepared from these two master context files:

1. `ProtoScope_Complete_Project_Context_Handover.md`
2. `ProtoScope_Revised_Final_Roadmap_Many_to_Many_Conversion.md`

The documents are reference context, not executable instructions. Where they
differ, the revised roadmap controls the development scope. In particular, the
current conversion commitment is the 20-route many-to-many matrix rather than a
small fixed set of demonstration pairs.

## 2. Product definition

ProtoScope is an ESP32-S3-based embedded gateway that connects, converts,
monitors, and logs communication across UART, CAN, RS-485, I2C, and SPI.

The first prototype will be a wired development-board prototype. It must prove
each interface independently before the common message abstraction and routing
engine are integrated.

## 3. Phase-0 decisions

| Decision | Phase-0 baseline |
|---|---|
| Interfaces | UART, CAN, RS-485, I2C, SPI |
| Controller | ESP32-S3 N16R8; module suffix confirmed, carrier revision pending |
| CAN architecture | ESP32-S3 TWAI controller plus SN65HVD230-based transceiver module |
| RS-485 architecture | UART plus automatic-direction half-duplex RS-485 module; exact revision pending verification |
| I2C gateway role | Controller mode for initial conversion routes |
| SPI gateway role | Controller/host mode for initial conversion routes |
| TFT | 2.8-inch, 240 x 320, ILI9341-compatible, SPI, preferably non-touch; exact module pending |
| Input | EC11-style rotary encoder with push switch; exact part pending |
| Storage | microSD, either on the TFT board or a separate module; exact arrangement pending |
| USB | Power and programming only |
| Power | USB-powered first prototype; no internal battery in current scope |
| Conversion coverage | All 10 unique pairs / 20 directed routes, enabled through configuration |
| First route | RT-01 UART -> CAN architecture-proof route |
| Wall-clock time | Not required initially; logs use boot-relative monotonic time unless a clock source is later approved |

The user confirmed the module suffix **N16R8** on 2026-11-10, replacing the
original N8R2 candidate. Configure 16 MB flash and 8 MB Octal PSRAM.
The complete module model, carrier revision and header pin labels still need
recording before external wiring.

## 4. Functional requirements

### Interface requirements

- **FR-001:** The prototype shall provide UART transmit and receive using a
  documented framing configuration.
- **FR-002:** The prototype shall provide Classic CAN transmit and receive
  through the ESP32-S3 TWAI peripheral and an external CAN transceiver.
- **FR-003:** The prototype shall provide half-duplex RS-485 transmit and receive
  through a verified transceiver module.
- **FR-004:** The prototype shall provide I2C controller-mode reads and writes
  to configured devices.
- **FR-005:** The prototype shall provide SPI controller/host-mode transactions
  to configured devices.
- **FR-006:** Passive I2C/SPI sniffing and I2C/SPI peripheral mode shall not be
  implied unless separately designed and tested.

### Routing and conversion requirements

- **FR-010:** The system shall represent all 20 directed routes between two
  different supported interfaces in a central route registry.
- **FR-011:** Same-interface forwarding shall not count toward the 20-route
  target.
- **FR-012:** Each enabled route shall identify its source device/transaction,
  decoder, validation rule, transformation, encoder, destination settings,
  timeout policy, and error policy.
- **FR-013:** Unconfigured or role-incompatible routes shall be rejected safely.
- **FR-014:** Routes shall be enabled or disabled through configuration rather
  than separate pair-specific application pipelines.
- **FR-015:** The system shall distinguish raw forwarding from semantic
  application-level conversion.
- **FR-016:** The system shall prevent routing loops and duplicate re-entry.
- **FR-017:** The system shall not require all 20 routes to operate
  simultaneously.
- **FR-018:** Each route shall have its own repeatable input vector, expected
  output, fixture, and test result before being marked complete.
- **FR-019:** Protocol-specific metadata and transaction boundaries shall be
  preserved through the common event abstraction.
- **FR-020:** Slow destinations shall have explicit queue, timeout,
  back-pressure, and dropped-message behavior.

### Monitoring and logging requirements

- **FR-030:** The system shall maintain per-interface and per-route receive,
  transmit, rejection, timeout, failure, and dropped-event counters.
- **FR-031:** The minimum TFT status view shall show system state, active route,
  source/destination interfaces, RX count, TX count, error/drop count, and SD
  logging state.
- **FR-032:** The minimum Phase-8 log set shall include startup/configuration
  events, accepted or rejected route inputs, transmit results, timeouts,
  interface errors, queue overflows, and logging/storage errors.
- **FR-033:** Initial log timestamps shall use monotonic time since boot. A real
  date/time shall not be shown unless a clock source is added and validated.
- **FR-034:** Communication processing shall be decoupled from display refresh
  and SD writes so UI/storage latency does not silently lose traffic.
- **FR-035:** Missing-card, full-card, write-failure, and logger-overflow states
  shall be observable.

### Local user-interface requirements

- **FR-040:** The rotary encoder shall support rotation and push input.
- **FR-041:** The UI shall expose only implemented features.
- **FR-042:** Unavailable interfaces and invalid routes shall be visibly marked
  unavailable rather than appearing operational.
- **FR-043:** USB shall not be presented as a production data bridge or PC
  application interface in the current scope.

## 5. Non-functional and safety requirements

- **NFR-001:** No ESP32-S3 GPIO shall be exposed to 5 V logic.
- **NFR-002:** Supply voltage and signal-logic voltage shall be verified
  separately for every connected module.
- **NFR-003:** Pin assignments, connector numbering, voltage labels, pull-ups,
  biasing, and termination shall be documented before wiring.
- **NFR-004:** CAN and RS-485 termination shall follow the actual bus topology,
  not be enabled on every node by default.
- **NFR-005:** Firmware communication paths shall be non-blocking where
  practical and expose overflow/error counters.
- **NFR-006:** Raw input shall remain available for debugging before
  transformation.
- **NFR-007:** Working changes and test evidence shall be recorded by milestone.
- **NFR-008:** The custom PCB shall not begin until the wired prototype pinout,
  power design, module revisions, firmware version, and integration tests are
  frozen.

## 6. Initial demonstration definition

The first demonstration is deliberately smaller than the final route target:

1. Boot the ESP32-S3 reliably.
2. Verify UART independently.
3. Verify CAN independently.
4. Execute RT-01 UART -> CAN using the exact framing and vector in the route
   matrix.
5. Expose route status and counters through development diagnostics.
6. Later, show the same live state on the TFT and log it to microSD.

This demonstration proves the architecture; it does not reduce the final
20-route commitment.

## 7. Phase-0 acceptance summary

Phase 0 is accepted as the requirements and system-design baseline. Physical
board/module revisions, electrical requirements, current budget, and fixtures
marked `TBD` remain explicit pre-wiring or later-phase gates; they are not
permission to infer an electrical connection. Current closure details are
tracked in [`phase-0-status.md`](phase-0-status.md).

