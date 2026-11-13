# Phase 5 — I2C and SPI interfaces (controller mode)

**Milestone:** M5 — I2C and SPI verified with known peripherals  
**Firmware:** 0.6.0 (Phase 5 code is complete in 0.5.x scope and shipped with Phase 6)  
**State:** implementation and host tests passed; ESP-IDF build and hardware acceptance pending

ProtoScope is an I2C **controller** and an SPI **controller/host** only (D-002,
FR-004, FR-005). Passive sniffing and peripheral/device mode are **not**
implemented and must not be claimed (FR-006). No routing or conversion is built.

## Consistency with the Phase-0 documents
| Item | Document | Phase-5 firmware |
|---|---|---|
| I2C SDA/SCL | [pinout](pinout-and-connectors.md): GPIO8 / GPIO9 | `PROTO_PIN_I2C_SDA/SCL` (I2C_NUM_0) |
| Ext. SPI SCLK/MOSI/MISO/CS0 | GPIO39 / 40 / 41 / 42 (default JTAG pad pins; USB-Serial-JTAG on GPIO19/20 is not used by this) | `PROTO_PIN_EXT_SPI_*` on **SPI3_HOST** |
| External SPI separate from TFT/microSD (D-004) | architecture | SPI2_HOST is left free for the Phase-9 UI bus; no pin is shared (GPIO11/12/13 + 7/10/14) |
| One configured SPI device initially | pinout | one chip select, CS0 |
| Pull-ups present exactly once | pinout/BOM | internal pull-ups **off** by default; `pullup` turns on the weak ESP pull-ups only for a bare device |
| Active, scheduled reads, not unsolicited RX | architecture §3 | `i2c read`/`wr`, `spi xfer` are explicit controller transactions |
| Pins stay parked until started | Phase 1 | both stay stopped until `i2c start` / `spi start` |
| Counters, timeouts (NFR-005) | requirements | per-bus counters, `last_error` name, 100 ms I2C timeout, non-blocking SPI polling for <= 64 bytes |

## Firmware behaviour
**I2C** (`proto_i2c`): 100 kHz (default) or 400 kHz, 7-bit addresses 0x08-0x77.
Operations: write (1-64 bytes), read (1-64), write-then-read (repeated start; the
usual register read). `i2c scan` probes 0x08-0x77. Counters: probes, devices
found, transactions, completed, nacks, timeouts, bus_errors, rejected, plus the
raw `last_error` (the exact ESP-IDF error for a NACK differs by version, so it
is recorded for you to characterize).

**SPI** (`proto_spi`): mode 0-3, 1 kHz-10 MHz (default mode 0, 1 MHz), 8-bit
words, MSB first, CS0 asserted for the whole 1-64 byte full-duplex transfer.
FIFO transfers (no DMA). Reboot to change mode or clock.

## Console (UART0, 115200)
```text
i2c start [100000|400000] [pullup]     i2c status     i2c scan
i2c write ADDR HEX                      i2c read ADDR LEN      i2c wr ADDR HEX LEN
spi start [MODE 0-3] [CLOCK_HZ]        spi status             spi xfer HEX
```
ADDR is a 7-bit hex address (`68` or `0x68`). Examples:
`i2c wr 68 75 1` (register 0x75, read 1 byte), `spi xfer 9F0000`.

## Pre-wiring gate
- [ ] Q-031: choose the I2C and SPI fixtures (programmable second MCU preferred;
  a documented sensor is fine for I2C read tests).
- [ ] Each fixture is 3.3 V (or level shifted). ESP32-S3 GPIO is not 5 V tolerant.
- [ ] For each I2C breakout: are pull-ups fitted? Exactly one set per bus (typically
  4.7-10 kohm to 3.3 V). Do not add more without checking.
- [ ] Record the exact fixture/breakout pinout.

## Test stages (power off before wiring)
### A. SPI loopback — no device needed
Jumper **GPIO40 (MOSI) to GPIO41 (MISO)**. In the monitor:
```
spi start 0 1000000
spi xfer 0102030405060708
```
Expect `spi_rx bytes=8 hex=0102030405060708`. Remove the jumper afterwards. Repeat
after reboot with other modes/clocks you intend to support (loopback works in all
four modes).

### B. I2C with a real device
Wire SDA to GPIO8, SCL to GPIO9, 3V3 to the device's 3.3 V pin and common GND.
Commonly available examples (check your own datasheet): an MPU-6050 at 0x68 returns
0x68 from register 0x75; a BMP280 at 0x76/0x77 returns 0x58 from register 0xD0.
```
i2c start 100000
i2c scan
i2c wr 68 75 1
```
Also test a missing address (`i2c read 50 1`): it must fail promptly with a counted
NACK/timeout, and `i2c status` must keep `bus_errors=0` for ordinary NACKs. Record
the raw `last_error` you observe for a NACK. Then `i2c write` and read back a
writable register or an EEPROM byte if your fixture has one.

### C. SPI with a real device
Wire SCLK GPIO39, MOSI GPIO40, MISO GPIO41, CS GPIO42, 3V3, GND. Use a device
with a known command, e.g. a SPI flash JEDEC read (`spi xfer 9F000000`, manufacturer
byte 0xEF for Winbond) or your second-MCU register model. Match the device's SPI mode.

### Script
```bash
python tools/test_phase5_6_fixture.py --console-port CONSOLE_PORT \
    --spi-loopback --log docs/test-reports/phase5-spi-loopback.log
python tools/test_phase5_6_fixture.py --console-port CONSOLE_PORT \
    --i2c-addr 68 --i2c-reg 75 --i2c-expect 68 --log docs/test-reports/phase5-i2c.log
```
(Run each as a separate invocation; every run reboots the board.)

## Acceptance state
- [x] I2C controller and SPI controller drivers, validation and counters implemented.
- [x] Address/frequency/mode/clock/transaction validation host-tested with sanitizers.
- [x] Driver sources syntax-checked against stand-in headers only.
- [ ] Builds against ESP-IDF v5.5.6 (`./tools/verify_phase6.sh`).
- [ ] Pre-wiring gate completed and recorded.
- [ ] SPI loopback and a real SPI device verified (mode, clock, chip-select behaviour documented).
- [ ] I2C scan, read, write and missing-device behaviour verified; NACK error recorded.
- [ ] Pull-up, voltage and bus-sharing notes updated in the pinout document.
M5 stays open until the hardware items pass.
