# Phase 9 — OLED display and rotary-encoder UI

**Milestone:** M9 — local UI shows live state; encoder works; UI never disturbs communication  
**Firmware:** 0.9.0  
**State:** implemented and host-tested (screens, menu, encoder decoding, OLED packing); build and hardware tests pending

## Display decision
The original BOM listed a 2.8 inch SPI TFT (ILI9341). The fitted display is a **4-pin 0.96 inch OLED**
(SSD1306, 128x64, I2C), so firmware 0.9.0 supports exactly that. The TFT driver is not included. The
display uses its own I2C bus so it never touches the external I2C port.

## Wiring (module pin names GND, VCC, SCL, SDA; read your module's labels, the order varies)
| OLED pin | ESP32-S3 |
|---|---|
| SDA | GPIO14 (header J1-20) |
| SCL | GPIO10 (header J1-16) |
| VCC | 3V3 |
| GND | GND |
Encoder (EC11): A to GPIO1, B to GPIO2, push switch to GPIO6, common/GND to GND (`+` pin unconnected; the
ESP's pull-ups are used). Full pin list: `docs/hardware/`.
Pin plan change: GPIO14 and GPIO10 were reserved for TFT D/C and CS. They now carry the OLED's SDA and SCL,
GPIO21 (TFT reset) is spare, and GPIO11/12/13 serve only the microSD card.

## Screens and controls
Turn = move or change value. Short press = select. Long press (0.7 s) = back.
| Screen | Shows | What you can do |
|---|---|---|
| Home (monitor) | each interface: `*` active, `!` warning, `-` off; received/sent and errors; routes enabled; SD state; a status line | short press opens the menu |
| Menu | Monitor, Interfaces, Routes, Logging, System | turn and select |
| Interfaces > setup | choose settings (UART baud, RS-485 baud and echo, CAN rate and mode, I2C frequency and pull-up, SPI mode and clock), then START | starts that interface with those settings; an active interface is read-only (reboot to change) |
| Routes | RT-01..RT-20: `ON`, `off` (ready) or `n/c` (not configured) | press to enable or disable a ready route; a not-configured route says so |
| Logging | SD state, file, records, drops, write errors | press to start or stop logging |
| System | firmware, uptime, heap, PSRAM, display | - |
A warning (SD problem or CAN errors) appears as a highlighted bar on the home screen. The menu lists only
features that exist; nothing is shown that does not work.

## Behaviour you can rely on
- The UI runs in its own low-priority task and refreshes about four times a second. Rendering and the I2C
  transfer take a few tens of milliseconds and never block UART, CAN, RS-485, routing or logging.
- If the OLED is not connected at boot, the UI keeps running and looks for it every 4 seconds, so you can plug
  it in later. If it stops answering, it is released and found again after you reseat it.
- Encoder direction reversed? `ui invert on` (or the menuconfig option) instead of rewiring.
- Picture upside down? `display flip on`.

## Console (useful without the encoder or the display)
```text
display start | display status | display test | display flip on|off
ui status | ui screen | ui press cw|ccw|short|long [count] | ui invert on|off
```
`ui screen` prints the text of the current screen; `ui press` injects an encoder input.
`display test` shows four corner squares and a border for 6 seconds.

## Tests
- Host: font and text rendering (read back pixel by pixel), OLED page packing, encoder detent decoding
  (clockwise, counter-clockwise, bounce, skipped edge), button debounce, short and long press, every screen,
  menu navigation, parameter editing, route and logging actions, messages.
- Hardware, console only: `python tools/test_phase8_9_fixture.py --console-port PORT --expect-display --log docs/test-reports/phase9.log`
  drives the whole menu with `ui press` and checks UART start, route enable/disable and logging from the screens.
- By hand with the real parts: turn the knob (each click moves one step in both directions), press (select),
  hold (back), check the picture on the OLED, unplug and re-plug the OLED.

## Not covered
Colour (the OLED is monochrome; warnings use a highlighted bar), changing a running interface's settings
without a reboot, and saving settings. The encoder pins have no external debouncing.

## Acceptance state
- [x] Display driver (SSD1306, probe, init, flush, flip, recovery), encoder driver, UI model and screens.
- [x] Menu lists only working features; unavailable features are clearly marked.
- [ ] Builds against ESP-IDF v5.5.6 (`./tools/verify_phase9.sh`).
- [ ] OLED shows live values; the encoder turns and presses reliably; UI activity does not disturb the tested communication and logging workload.
M9 closes when the hardware items pass.
