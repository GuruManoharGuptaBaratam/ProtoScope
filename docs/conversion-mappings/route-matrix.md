# Conversion route matrix

**Document status:** Phase-0 baseline, revision 0.1  
**Coverage:** 10 unique interface pairs / 20 directed routes  
**Last updated:** 2026-11-10

## 1. Route semantics

Every row is a separate directed capability. The reverse direction is not
assumed to be an automatic inverse.

For the initial gateway mode:

- UART source/destination means framed byte-stream receive/transmit.
- CAN source/destination means filtered frame receive/configured frame transmit.
- RS-485 source/destination means framed half-duplex serial receive/transmit.
- I2C source means a scheduled controller read; I2C destination means a
  controller write.
- SPI source means a scheduled controller read; SPI destination means a
  controller write.

I2C/SPI peripheral mode and passive sniffing are outside the initial route
semantics.

## 2. Complete matrix

`TBD` is intentional. A route remains disabled until its device-level formats,
mapping, settings, and fixtures are defined.

| ID | Route | Source operating mode and format | Destination operating mode and format | Mapping requirement | Repeatable fixture | Build order / state |
|---|---|---|---|---|---|---|
| RT-01 | UART -> CAN | UART RX, 115200 8N1, `PS-UART-CAN/1` newline frame | Classic CAN, standard ID, 500 kbit/s initial test | Parse ID/DLC/data and construct CAN frame | 3.3 V UART adapter + known CAN receiver | 1 / specified below |
| RT-02 | CAN -> UART | Filtered Classic CAN frame; exact filter TBD | UART TX framed event; schema TBD | Preserve ID/flags/DLC/data in serial representation | Known CAN transmitter + UART adapter | 2 / format TBD |
| RT-03 | UART -> RS-485 | UART framed message; schema TBD | Half-duplex RS-485 serial frame; schema/baud TBD | Raw or field mapping TBD | UART adapter + RS-485 test node | 3 / TBD |
| RT-04 | RS-485 -> UART | Half-duplex serial frame; raw or Modbus profile TBD | UART framed message; schema TBD | Decode/validate/re-encode TBD | RS-485 test node + UART adapter | 6 / TBD |
| RT-05 | CAN -> RS-485 | Filtered Classic CAN frame; IDs TBD | Half-duplex serial frame; protocol TBD | CAN fields to serial/application fields TBD | CAN node + RS-485 node | 7 / TBD |
| RT-06 | RS-485 -> CAN | Half-duplex serial frame; raw or Modbus profile TBD | Classic CAN frame; ID/payload TBD | Serial/application fields to CAN fields TBD | RS-485 node + CAN node | 8 / TBD |
| RT-07 | UART -> I2C | UART framed command; schema TBD | Controller write to address/register TBD | Command fields to I2C write bytes TBD | UART adapter + I2C peripheral fixture | 9 / TBD |
| RT-08 | I2C -> UART | Scheduled controller read; address/register/length TBD | UART framed event; schema TBD | I2C value/metadata to serial representation TBD | I2C peripheral fixture + UART adapter | 10 / TBD |
| RT-09 | CAN -> I2C | Filtered Classic CAN frame; IDs TBD | Controller write to address/register TBD | CAN fields to I2C command/value TBD | CAN node + I2C peripheral fixture | 11 / TBD |
| RT-10 | I2C -> CAN | Scheduled controller read; address/register/length TBD | Classic CAN frame; ID/payload TBD | I2C value/scaling to CAN fields TBD | I2C peripheral fixture + CAN node | 12 / TBD |
| RT-11 | RS-485 -> I2C | Half-duplex serial frame; protocol TBD | Controller write to address/register TBD | Serial/application fields to I2C command/value TBD | RS-485 node + I2C peripheral fixture | 13 / TBD |
| RT-12 | I2C -> RS-485 | Scheduled controller read; address/register/length TBD | Half-duplex serial frame; protocol TBD | I2C value to serial/application fields TBD | I2C peripheral fixture + RS-485 node | 14 / TBD |
| RT-13 | UART -> SPI | UART framed command; schema TBD | Controller write transaction; CS/mode/command TBD | Command fields to SPI transaction TBD | UART adapter + SPI peripheral fixture | 15 / TBD |
| RT-14 | SPI -> UART | Scheduled controller read; CS/mode/command/length TBD | UART framed event; schema TBD | SPI response/metadata to serial representation TBD | SPI peripheral fixture + UART adapter | 16 / TBD |
| RT-15 | CAN -> SPI | Filtered Classic CAN frame; IDs TBD | Controller write transaction; CS/mode/command TBD | CAN fields to SPI command/value TBD | CAN node + SPI peripheral fixture | 17 / TBD |
| RT-16 | SPI -> CAN | Scheduled controller read; CS/mode/command/length TBD | Classic CAN frame; ID/payload TBD | SPI value/scaling to CAN fields TBD | SPI peripheral fixture + CAN node | 18 / TBD |
| RT-17 | RS-485 -> SPI | Half-duplex serial frame; protocol TBD | Controller write transaction; CS/mode/command TBD | Serial/application fields to SPI command/value TBD | RS-485 node + SPI peripheral fixture | 19 / TBD |
| RT-18 | SPI -> RS-485 | Scheduled controller read; CS/mode/command/length TBD | Half-duplex serial frame; protocol TBD | SPI value to serial/application fields TBD | SPI peripheral fixture + RS-485 node | 20 / TBD |
| RT-19 | I2C -> SPI | Scheduled controller read; address/register/length TBD | Controller write transaction; CS/mode/command TBD | I2C value/metadata to SPI command/value TBD | I2C + SPI programmable peripheral fixtures | 4 / representative architecture proof; formats TBD |
| RT-20 | SPI -> I2C | Scheduled controller read; CS/mode/command/length TBD | Controller write to address/register TBD | SPI value/metadata to I2C command/value TBD | SPI + I2C programmable peripheral fixtures | 5 / representative architecture proof; formats TBD |

## 3. RT-01 UART -> CAN specification

RT-01 is the first architecture-proof route. It is a test envelope, not a claim
of semantic conversion for an arbitrary UART device.

### Route configuration

| Field | Value |
|---|---|
| Route ID | `RT-01` |
| Enable state | Disabled by default until UART and CAN interface tests pass |
| Source | External UART RX |
| UART settings | 115200 baud, 8 data bits, no parity, 1 stop bit |
| Message boundary | LF (`\n`); optional preceding CR accepted |
| Maximum line length | 64 bytes including delimiter |
| Partial-frame timeout | 200 ms from the most recent byte |
| Destination | Classic CAN through TWAI |
| CAN test rate | 500 kbit/s, configurable to match the fixture |
| CAN frame type | Standard 11-bit data frame only |
| CAN payload length | 0-8 bytes |
| Queue policy | Enqueue one validated frame; reject and count when queue is full |
| Retry policy | No blind application retry; record TWAI result/error and let the CAN driver recovery policy act |

### Source grammar

```text
CAN,<id_hex>,<dlc_dec>,<payload_hex>\n
```

Validation rules:

- `id_hex` is `000` through `7FF` (case-insensitive input accepted).
- `dlc_dec` is `0` through `8`.
- `payload_hex` contains exactly `2 * dlc` hexadecimal characters.
- No whitespace is permitted inside fields.
- Extra fields, invalid hex, length mismatch, overlong lines, and timeouts are
  rejected without CAN transmission.
- DLC 0 uses an empty payload field: `CAN,123,0,\n`.

### Mapping

| Source field | Destination field | Transformation |
|---|---|---|
| `id_hex` | CAN standard identifier | Hex string to unsigned 11-bit value |
| `dlc_dec` | CAN DLC | Decimal string to integer |
| `payload_hex` | CAN data bytes | Consecutive two-digit hex pairs to bytes |

This first route is structural mapping only. It does not assign engineering
units or application-level meaning to the payload.

### Positive test vector

```text
UART input: CAN,123,3,0105FF\n
Expected CAN output:
  Identifier: 0x123 (standard)
  DLC:        3
  Data:       01 05 FF
```

### Required negative vectors

| Input | Expected behavior |
|---|---|
| `CAN,800,1,00\n` | Reject: identifier out of standard range |
| `CAN,123,3,0105\n` | Reject: payload/DLC mismatch |
| `CAN,XYZ,1,00\n` | Reject: invalid identifier |
| `CAN,123,9,000000000000000000\n` | Reject: DLC out of range |
| partial line followed by 200 ms inactivity | Reject: source timeout |
| valid line while destination queue is full | Reject/count queue overflow; no partial send |

### Observability

For every input, increment the appropriate route counters. Log the monotonic
timestamp, route ID, acceptance/rejection status, parsed identifier/DLC when
available, and final CAN transmit result. Never log a transmit success before
the CAN driver reports it.

### Fixture gate

RT-01 cannot be executed until the exact 3.3 V UART adapter and CAN receiver are
identified, CAN termination is documented, and UART/CAN independent interface
tests have passed.

## 4. Staged implementation order

### Batch A — architecture proof

1. RT-01 UART -> CAN
2. RT-02 CAN -> UART
3. RT-03 UART -> RS-485
4. RT-19 I2C -> SPI
5. RT-20 SPI -> I2C

This batch deliberately covers byte-stream, framed-bus, half-duplex, and
controller-initiated transaction semantics.

### Batch B — remaining serial/CAN routes

6. RT-04 RS-485 -> UART
7. RT-05 CAN -> RS-485
8. RT-06 RS-485 -> CAN

### Batch C — remaining I2C routes

9. RT-07 UART -> I2C
10. RT-08 I2C -> UART
11. RT-09 CAN -> I2C
12. RT-10 I2C -> CAN
13. RT-11 RS-485 -> I2C
14. RT-12 I2C -> RS-485

### Batch D — remaining SPI routes

15. RT-13 UART -> SPI
16. RT-14 SPI -> UART
17. RT-15 CAN -> SPI
18. RT-16 SPI -> CAN
19. RT-17 RS-485 -> SPI
20. RT-18 SPI -> RS-485

### Batch E — full regression

Re-run all 20 route-specific positive, negative, timeout, unavailable-device,
and queue-pressure tests after integration. Registry presence alone is not
completion evidence.

