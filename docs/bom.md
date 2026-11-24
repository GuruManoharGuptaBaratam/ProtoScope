# Preliminary bill of materials

**Document status:** Phase-0 candidate BOM; not a purchase order  
**Last updated:** 2026-11-10

`Confirmed` means the project decision is stable and documentation is available.
`Candidate` means the part family/model is selected provisionally. `TBD` means
the exact item must not be guessed.

| Subsystem | Candidate / identifier | Qty | Status | Evidence still required |
|---|---|---:|---|---|
| Main controller | ESP32-S3 N16R8 | 1 | Module suffix confirmed by user, 2026-11-10; 16 MB flash / 8 MB Octal PSRAM | Full module model, carrier revision, seller SKU, USB connector type |
| CAN physical layer | Waveshare SN65HVD230 CAN board or equivalent verified SN65HVD230 breakout | 1 | Candidate | Exact product/SKU, schematic, pinout, termination population, supply/current |
| RS-485 physical layer | ChipTronicX TTL-to-RS485 3.3 V/5 V hardware auto-control module | 1 | Candidate | Actual revision, pinout, transceiver IC, logic thresholds, auto-direction limits, termination/biasing |
| Display | 2.8-inch 240 x 320 ILI9341-compatible SPI TFT, non-touch preferred | 1 | Candidate class; exact product TBD | Exact controller, pinout, voltage, backlight current, SD socket details |
| User input | EC11-family rotary encoder with push switch and knob | 1 | Candidate class | Exact part, detents/pulses, pinout, mounting dimensions |
| Storage | 16 GB or 32 GB microSD card | 1 | Capacity class TBD | Card type, filesystem choice, current/latency test |
| SD interface | TFT-board socket or separate 3.3 V-compatible SD module | 0 or 1 | TBD | Confirm TFT board; avoid duplicate hardware |
| CAN connector | DB9 or keyed alternative | 1 | Connector/pinout TBD | Exact family, gender, mounting, ProtoScope-specific pinout |
| RS-485 connector | DB9 or keyed alternative | 1 | Connector/pinout TBD | Exact family, gender, mounting, ProtoScope-specific pinout |
| UART connector | Keyed 4-pin connector/header | 1 | TBD | Family, pitch, mating housing, pin numbering |
| I2C connector | Keyed 4-pin connector/header | 1 | TBD | Family, pitch, mating housing, pin numbering |
| SPI connector | Keyed 6-pin connector/header | 1 | TBD | Family, pitch, mating housing, pin numbering |
| USB data cable | Cable matching the actual board | 1 | TBD until board inspection | Connector type and data capability |
| Prototype assembly | Breadboard/perfboard, headers, wire, terminals, standoffs | As required | TBD | Quantities after physical layout |
| CAN termination | 120-ohm resistor/control as required | As required | Topology-dependent | Confirm what is fitted to the module and test bus |
| RS-485 termination/bias | Values/components as required | As required | Topology-dependent | Confirm what is fitted to the module and test bus |
| I2C pull-ups | Value selected for bus voltage/capacitance | As required | Topology-dependent | Check every breakout before adding |
| Decoupling/protection | Capacitors and ESD/protection parts | As required | Design-dependent | Module schematics and prototype wiring review |

## Required test fixtures

| Interface | Recommended Phase-0 fixture strategy | Exact item status |
|---|---|---|
| UART | 3.3 V USB-to-UART adapter or second MCU | TBD based on available equipment |
| CAN | USB-CAN analyzer or second MCU plus verified CAN transceiver | TBD |
| RS-485 | USB-RS485 adapter or second MCU plus transceiver | TBD |
| I2C | Configurable second MCU as I2C peripheral; optional documented sensor for read tests | TBD |
| SPI | Configurable second MCU as SPI peripheral with a documented test register/command model | TBD |

A programmable second-MCU fixture is preferred for I2C and SPI because it can
implement deterministic read and write semantics needed to validate both route
directions. Its exact board must be selected before route formats are frozen.

## Procurement gate

Before ordering a consolidated batch:

- record a stable product URL/SKU and save the pinout/datasheet for each module;
- check supply and logic voltages separately;
- calculate the USB/regulator current budget including TFT backlight and SD
  write peaks;
- confirm whether the TFT already includes the required microSD socket;
- confirm connector mating parts and pin numbering;
- identify test fixtures for every interface;
- avoid buying termination, pull-up, bias, or level-shifting parts blindly.

## Update (firmware 0.9.0): display
The display in the build is a **4-pin 0.96 inch OLED, SSD1306, I2C** (replacing the 2.8 inch SPI TFT listed
above). The microSD card is used through a separate 3.3 V SPI module. Wiring: see
[`hardware/ProtoScope_Phase1-9_netlist.md`](hardware/ProtoScope_Phase1-9_netlist.md).
