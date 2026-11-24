# ProtoScope wiring netlist, Phases 1-9 (bench prototype)

**Firmware:** 0.9.0  
**Status:** wiring plan, not yet verified on hardware  

Header pin numbers follow the Espressif ESP32-S3-DevKitC-1 v1.1 user guide (J1 and J3, pin 1 at the end marked on the board). Verify against your board's silkscreen, and verify every module pin label on your exact modules, before powering anything. This follows `pinout-and-connectors.md`; do not treat it as frozen until that document's freeze checks are complete.

## Parts

| Ref | Part | Notes |
|---|---|---|
| U1 | ESP32-S3-DevKitC-1, N16R8 | USB-C on the UART/COM port to the computer |
| U2 | 3.3 V USB-UART adapter | Phase 2 test node |
| U3 | SN65HVD230 CAN module | Phase 3 |
| U4 | USB-CAN adapter | Second CAN node |
| U5 | Auto-direction RS-485 module | Phase 4 |
| U6 | USB-RS485 adapter | Second RS-485 node |
| U7 | I2C device (example GY-521 MPU-6050) | Phase 5, 3.3 V |
| U8 | SPI device (example W25Qxx flash module) | Phase 5, 3.3 V |
| U9 | 0.96 in 4-pin OLED, SSD1306, I2C (GND VCC SCL SDA) | Phase 9 |
| U10 | microSD module, 3.3 V SPI, FAT32 card | Phase 8, shares the display SPI bus |
| U11 | EC11 rotary encoder with push switch | Phase 9 |
| R1, R2 | 120 ohm | CAN bus ends: module jumper and adapter |
| R3, R4 | 120 ohm | RS-485 bus ends: module and adapter (check what is fitted) |
| R5, R6 | 4.7 kohm | I2C pull-ups, only if the device has none |
| JA1-JA4 | jumper wires | Setup A loopback tests only |

## U1 header pins used

| Header pin | Signal | Net |
|---|---|---|
| J1-1 | 3V3 | 3V3 |
| J1-2 | 3V3 | 3V3 |
| J1-4 | GPIO4 | CAN_TXD |
| J1-5 | GPIO5 | CAN_RXD |
| J1-6 | GPIO6 | ENC_SW |
| J1-7 | GPIO7 | SD_CS |
| J1-8 | GPIO15 | RS485_TXD |
| J1-9 | GPIO16 | RS485_RXD |
| J1-10 | GPIO17 | UART_TX |
| J1-11 | GPIO18 | UART_RX |
| J1-12 | GPIO8 | I2C_SDA |
| J1-15 | GPIO9 | I2C_SCL |
| J1-16 | GPIO10 | OLED_SCL |
| J1-17 | GPIO11 | UI_MOSI |
| J1-18 | GPIO12 | UI_SCLK |
| J1-19 | GPIO13 | UI_MISO |
| J1-20 | GPIO14 | OLED_SDA |
| J1-22 | GND | GND |
| J3-1 | GND | GND |
| J3-4 | GPIO1 | ENC_A |
| J3-5 | GPIO2 | ENC_B |
| J3-6 | GPIO42 | SPI_CS0 |
| J3-7 | GPIO41 | SPI_MISO |
| J3-8 | GPIO40 | SPI_MOSI |
| J3-9 | GPIO39 | SPI_SCLK |
| J3-21 | GND | GND |
| J3-22 | GND | GND |

## Netlist

### U2 3.3 V USB-UART adapter (Phase 2)

| Net | From | To | Direction / note |
|---|---|---|---|
| UART_TX | U1 J1-10 (GPIO17) | U2 RXD | ESP to module |
| UART_RX | U1 J1-11 (GPIO18) | U2 TXD | module to ESP |
| GND | U1 J1-22 (or J3-1/21/22) | U2 GND | ground |
| NC | - | U2 VCC / 5V | leave unconnected |

- Adapter must be set to 3.3 V logic. Wire only RXD, TXD and GND.
- Adapter TXD drives the ESP RX pin; ESP TX drives adapter RXD (crossed).

### U3 SN65HVD230 CAN module (Phase 3)

| Net | From | To | Direction / note |
|---|---|---|---|
| CAN_TXD | U1 J1-4 (GPIO4) | U3 TX (D) | ESP to module |
| CAN_RXD | U1 J1-5 (GPIO5) | U3 RX (R) | module to ESP |
| 3V3 | U1 J1-1 (or J1-2) | U3 3.3V | power, 3.3 V only |
| GND | U1 J1-22 (or J3-1/21/22) | U3 GND | ground |
| CANH | U3 CANH | U4 CANH | differential bus pair |
| CANL | U3 CANL | U4 CANL | differential bus pair |
| GND | U1 GND | U4 GND | optional/reference; use if the adapter has one |
| bus end | across the pair | R1 120 ohm | module termination jumper |
| bus end | across the pair | R2 120 ohm | adapter termination |

- Module TX/RX labels are from the module side: ESP TX goes to module TX.
- Terminate only the two ends of the bus. Powered off, CANH-CANL reads about 60 ohm.

### U5 RS-485 module (auto-direction) (Phase 4)

| Net | From | To | Direction / note |
|---|---|---|---|
| RS485_TXD | U1 J1-8 (GPIO15) | U5 TXD (data in) | ESP to module |
| RS485_RXD | U1 J1-9 (GPIO16) | U5 RXD (data out) | module to ESP |
| 3V3 | U1 J1-1 (or J1-2) | U5 VCC (use 3V3) | power, 3.3 V only |
| GND | U1 J1-22 (or J3-1/21/22) | U5 GND | ground |
| RS485_A | U5 A | U6 A (+) | differential bus pair |
| RS485_B | U5 B | U6 B (-) | differential bus pair |
| GND | U1 GND | U6 GND | optional/reference; use if the adapter has one |
| bus end | across the pair | R3 120 ohm | module end (check board) |
| bus end | across the pair | R4 120 ohm | adapter end (check board) |

- Power from 3V3 so RXD idles at 3.3 V. Measure it before connecting GPIO16.
- No DE/RE pin is driven. Bias per your module/adapter documentation.

### U7 I2C device (example: GY-521 MPU-6050) (Phase 5)

| Net | From | To | Direction / note |
|---|---|---|---|
| I2C_SDA | U1 J1-12 (GPIO8) | U7 SDA | bidirectional |
| I2C_SCL | U1 J1-15 (GPIO9) | U7 SCL | ESP to module |
| 3V3 | U1 J1-1 (or J1-2) | U7 VCC | power, 3.3 V only |
| GND | U1 J1-22 (or J3-1/21/22) | U7 GND | ground |
| GND | U1 J1-22 (or J3-1/21/22) | U7 AD0 | ground |
| NC | - | U7 INT / XDA / XCL | leave unconnected |

- AD0 to GND gives address 0x68. Pull-ups exactly once per bus.
- If the device has none: R5, R6 = 4.7 kohm from SDA and SCL to 3V3 (or 'i2c start 100000 pullup').

### U8 SPI device (example: W25Qxx flash module) (Phase 5)

| Net | From | To | Direction / note |
|---|---|---|---|
| SPI_SCLK | U1 J3-9 (GPIO39) | U8 CLK | ESP to module |
| SPI_MOSI | U1 J3-8 (GPIO40) | U8 DI (MOSI) | ESP to module |
| SPI_MISO | U1 J3-7 (GPIO41) | U8 DO (MISO) | module to ESP |
| SPI_CS0 | U1 J3-6 (GPIO42) | U8 CS | ESP to module |
| 3V3 | U1 J1-1 (or J1-2) | U8 VCC | power, 3.3 V only |
| GND | U1 J1-22 (or J3-1/21/22) | U8 GND | ground |
| 3V3 | U1 J1-1 (or J1-2) | U8 WP | power, 3.3 V only |
| 3V3 | U1 J1-1 (or J1-2) | U8 HOLD | power, 3.3 V only |

- Use your device's pin names (SCK/SDI/SDO/SS etc.). Match its SPI mode: 'spi start MODE CLOCK'.
- A flash module needs WP and HOLD tied high for plain SPI. Other devices differ.

### U9 0.96 in OLED, SSD1306 (4-pin, I2C) (Phase 9)

| Net | From | To | Direction / note |
|---|---|---|---|
| OLED_SDA | U1 J1-20 (GPIO14) | U9 SDA | bidirectional |
| OLED_SCL | U1 J1-16 (GPIO10) | U9 SCL | ESP to module |
| 3V3 | U1 J1-1 (or J1-2) | U9 VCC | power, 3.3 V only |
| GND | U1 J1-22 (or J3-1/21/22) | U9 GND | ground |

- Module pin order varies: read the labels (GND, VCC, SCL, SDA) on your board, not the position.
- Own I2C bus (I2C_NUM_1) on the pins once reserved for TFT D/C and CS. Not the external I2C pins GPIO8/9.
- Module normally has its own pull-ups; address 0x3C or 0x3D. Power from 3V3.

### U10 microSD module (3.3 V SPI) (Phase 8)

| Net | From | To | Direction / note |
|---|---|---|---|
| UI_SCLK | U1 J1-18 (GPIO12) | U10 SCK | ESP to module |
| UI_MOSI | U1 J1-17 (GPIO11) | U10 MOSI | ESP to module |
| UI_MISO | U1 J1-19 (GPIO13) | U10 MISO | module to ESP |
| SD_CS | U1 J1-7 (GPIO7) | U10 CS | ESP to module |
| 3V3 | U1 J1-1 (or J1-2) | U10 VCC | power, 3.3 V only |
| GND | U1 J1-22 (or J3-1/21/22) | U10 GND | ground |

- The card has its own SPI bus (SPI2) separate from the external SPI port. FAT32 card.
- A module with a 5 V regulator and level shifter may need the 5V pin: check that MISO idles at 3.3 V.

### U11 EC11 rotary encoder with push switch (Phase 9)

| Net | From | To | Direction / note |
|---|---|---|---|
| ENC_A | U1 J3-4 (GPIO1) | U11 A (CLK) | module to ESP |
| ENC_B | U1 J3-5 (GPIO2) | U11 B (DT) | module to ESP |
| ENC_SW | U1 J1-6 (GPIO6) | U11 SW | module to ESP |
| GND | U1 J1-22 (or J3-1/21/22) | U11 C / GND | ground |
| NC | - | U11 + (if present) | leave unconnected |

- Internal pull-ups are used; the switch closes to GND. A bare EC11: A, B, C (common) and two switch pins.
- If turning feels reversed, run 'ui invert on' instead of rewiring.

## Setup A loopback jumpers (firmware-only tests, no modules on these pins)

| Ref | Between | Tests | Condition |
|---|---|---|---|
| JA1 | J1-10 (GPIO17) and J1-11 (GPIO18) | UART1 loopback | remove U2 first |
| JA2 | J1-4 (GPIO4) and J1-5 (GPIO5) | CAN self-test loopback | remove U3 first |
| JA3 | J1-8 (GPIO15) and J1-9 (GPIO16) | RS-485 UART2 loopback | remove U5 first |
| JA4 | J3-8 (GPIO40) and J3-7 (GPIO41) | SPI MOSI-MISO loopback | remove U8 first |

Never leave a jumper in place while the module for the same pins is connected: two outputs would drive one wire.

## Pins that stay unconnected through Phase 9

GPIO21 and GPIO47 spare; GPIO19/20 (native USB), GPIO43/44 (console); GPIO0, 3, 45, 46 (strapping); GPIO35-37 (octal memory); GPIO38 and 48 (possible RGB LED). Never apply 5 V to any GPIO.

## Pre-power checklist

- [ ] Board silkscreen matches the header pin numbers above (or note the differences).
- [ ] Every module's pin labels checked against its own markings or schematic.
- [ ] USB-UART adapter is in 3.3 V mode and its VCC/5V pin is unconnected.
- [ ] RS-485 module powered from 3V3; its RXD output measured at about 3.3 V before connecting to J1-9.
- [ ] Only one pull-up set on SDA/SCL; only two terminations on each differential bus.
- [ ] No loopback jumper remains across a pin pair that a module is connected to.
- [ ] Multimeter: no short between 3V3 and GND at the board header before applying power.
- [ ] OLED pin order checked against the module's own labels (GND, VCC, SCL, SDA).
- [ ] microSD module is a 3.3 V logic part (or its MISO output measured at 3.3 V).
