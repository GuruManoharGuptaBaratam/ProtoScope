#!/usr/bin/env python3
"""Phase 8/9 console acceptance (pyserial only).

Phase 8 needs a FAT-formatted microSD card on the UI SPI bus (CS GPIO7) and an SPI
loopback jumper GPIO40 <-> GPIO41 to generate events without any other device.
Phase 9 is driven through 'ui press' / 'ui screen' so it works with or without the OLED
and with or without the encoder wired (the real encoder is tested by hand, see the docs).
--expect-display additionally requires the 0.96 inch OLED (SDA GPIO14, SCL GPIO10) to be found.
Optional: --card-dir PATH checks the newest LOGnnnn.CSV after you re-insert the card.
Saves a transcript. See docs/phase-8-monitor-logger.md and docs/phase-9-ui.md.
"""

import argparse
import csv
import re
import time
from datetime import datetime, timezone
from pathlib import Path


class Console:
    def __init__(self, port, log):
        self.port, self.log = port, log

    def command(self, command, pattern, timeout=6):
        self.port.reset_input_buffer()
        self.log.write(f"\n> {command}\n")
        self.log.flush()
        self.port.write((command + "\n").encode("ascii"))
        deadline, result = time.monotonic() + timeout, ""
        while time.monotonic() < deadline:
            chunk = self.port.read(1024).decode("utf-8", errors="replace")
            if chunk:
                self.log.write(chunk)
                self.log.flush()
                result += chunk
                if "\x1b[6n" in chunk:
                    self.port.write(b"\x1b[1;1R")
                if re.search(pattern, result):
                    return result
        raise TimeoutError(f"No {pattern!r} response to {command!r}: {result[-1200:]}")

    def kv(self, command, end_marker, line_prefix):
        response = self.command(command, end_marker)
        line = next(l for l in response.splitlines() if l.startswith(line_prefix))
        return {k: v for k, v in re.findall(r"(\w+)=(\S+)", line)}

    def screen(self):
        response = self.command("ui screen", r"ui_screen_end")
        return [m for m in re.findall(r"ui_row\d+=\|(.*)\|", response)]

    def press(self, what, count=1):
        self.command(f"ui press {what} {count}", r"ui_press=ok")
        time.sleep(0.7)


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def phase8(console, card_dir):
    mon = console.command("monitor", r"monitor_end")
    for proto in ("UART", "RS485", "CAN", "I2C", "SPI"):
        expect(f"monitor_if proto={proto} " in mon, f"monitor missing {proto}")
    expect("monitor_routes defined=20" in mon and "monitor_log state=off" in mon, "monitor routes/log lines")
    console.command("spi start 0 1000000", r"spi_start=ok")
    console.command("spi xfer 0102030405060708", r"spi_rx bytes=8 hex=0102030405060708")   # jumper check
    started = console.command("log start", r"log_start=\w+")
    state = re.search(r"log_start=(\w+)", started)[1]
    expect(state == "running", f"log start returned {state}: insert a FAT-formatted card (CS GPIO7)")
    for _ in range(10):
        console.command("msg send spi 01020304", r"msg_result")
    time.sleep(2.0)
    status = console.kv("log status", r"log_status_end", "records=")
    records = int(status["records"])
    expect(records >= 20 and int(status["dropped_total"]) == 0 and int(status["write_errors"]) == 0,
           f"log counters: {status}")
    head = console.kv("log status", r"log_status_end", "log_state=")
    expect(head["log_state"] == "running" and re.fullmatch(r"LOG\d{4}\.CSV", head["file"]), f"log state: {head}")
    mon = console.command("monitor", r"monitor_end")
    expect("monitor_log state=running" in mon and "dropped=0" in mon, "monitor must show the running log")
    # filter: SPI events must stop being logged
    before = records
    console.command("log filter uart can", r"log_filter=ok")
    console.command("msg send spi 01020304", r"msg_result")
    time.sleep(1.5)
    expect(int(console.kv("log status", r"log_status_end", "records=")["records"]) == before,
           "filtered protocol must not be logged")
    console.command("log filter all", r"log_filter=ok")
    console.command("log stop", r"log_stop=ok")
    expect("log_state=off" in console.command("log status", r"log_status_end"), "log must stop")
    name = head["file"]
    if card_dir:
        path = Path(card_dir) / name
        expect(path.exists(), f"{path} not found on the card")
        lines = path.read_text().splitlines()
        expect(lines[0].startswith("# ProtoScope log schema=1"), "header line 1")
        expect(lines[2].startswith("timestamp_us,event_id,correlation_id,protocol"), "column row")
        rows = list(csv.reader(lines[3:]))
        expect(len(rows) == records and all(len(r) == 12 for r in rows), f"{len(rows)} rows vs {records} records")
        expect(all(r[3] == "SPI" for r in rows) and {r[4] for r in rows} == {"TX", "RX"}, "SPI TX and RX rows")
        expect(all(int(r[0]) > 0 for r in rows), "timestamps must be set")
    return name


def phase9(console):
    status = console.kv("ui status", r"ui_status_end", "ui_started=")
    expect(status["ui_started"] == "yes", "UI task must be running")
    rows = console.screen()
    expect(rows and rows[0].startswith("PROTOSCOPE "), f"home screen: {rows}")
    console.press("short")
    expect(console.screen()[0] == "MENU", "short press opens the menu")
    console.press("cw")
    console.press("short")
    expect(console.screen()[0] == "INTERFACES", "Interfaces screen")
    console.press("short")
    expect(console.screen()[0] == "UART SETUP", "UART setup")
    console.press("cw")                                   # cursor to START
    console.press("short")
    expect("uart_active=yes" in console.command("status", r"ui_status_end"), "UART started from the screen")
    console.press("long")
    expect(console.screen()[0] == "MENU", "long press goes back to the menu")
    console.press("cw")                                   # Routes
    console.press("short")
    expect(console.screen()[0].startswith("ROUTES"), "Routes screen")
    console.press("short")                                # RT-01 is ready by default: enable
    expect("route_id=RT-01 src=UART dst=CAN enabled=yes" in console.command("route list", r"route_list_end"), "RT-01 enabled by the UI")
    console.press("short")                                # disable again
    expect("route_id=RT-01 src=UART dst=CAN enabled=no" in console.command("route list", r"route_list_end"), "RT-01 disabled by the UI")
    console.press("long")
    console.press("cw")                                   # Logging
    console.press("short")
    expect(console.screen()[0] == "LOGGING", "Logging screen")
    console.press("short")                                # start logging (running, or NO CARD)
    expect("wanted=yes" in console.command("log status", r"log_status_end"), "logging requested from the screen")
    console.press("short")                                # stop
    expect("wanted=no" in console.command("log status", r"log_status_end"), "logging stopped from the screen")
    console.press("long")
    console.press("long")
    expect(console.screen()[0].startswith("PROTOSCOPE "), "long presses return home")
    final = console.kv("ui status", r"ui_status_end", "ui_started=")
    expect(int(final["max_frame_ms"]) < 250, f"frame time too high: {final}")
    return console.kv("display status", r"ui_status_end", "display_active=")


def run(console, card_dir, do_8, do_9, expect_display):
    console.command("reboot", r"ProtoScope Phase 1 foundation ready", timeout=20)
    time.sleep(3)
    status = console.command("status", r"ui_status_end")
    for needed in ("psram_initialized: yes", "log_state=off", "ui_started=yes"):
        expect(needed in status, f"Foundation check missing: {needed}")
    if do_8:
        phase8(console, card_dir)
    if do_9:
        info = phase9(console)
        console.log.write(f"\nDisplay report: {info}\n")
        if expect_display:
            expect(info["display_active"] == "yes" and info["type"] == "SSD1306",
                   f"OLED not found (SDA GPIO14, SCL GPIO10, 3V3, GND): {info}")
            expect(int(info["flushes"]) > 0 and int(info["flush_errors"]) == 0, f"display flushes: {info}")
            console.command("display test", r"display_test=shown")
            console.log.write("\nLook at the OLED now: 4 corner squares and a border should be visible for 6 seconds.\n")
            time.sleep(7)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console-port", required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--card-dir", help="mounted card path to verify the newest log file")
    parser.add_argument("--expect-display", action="store_true", help="require the OLED to be found and refreshing")
    parser.add_argument("--skip-logger", action="store_true")
    parser.add_argument("--skip-ui", action="store_true")
    args = parser.parse_args()
    import serial

    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write(f"UTC test start: {datetime.now(timezone.utc).isoformat()}\n")
        with serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2) as board:
            board.dtr = board.rts = False
            board.port = args.console_port
            board.open()
            time.sleep(2)
            try:
                run(Console(board, log), args.card_dir, not args.skip_logger, not args.skip_ui, args.expect_display)
            except Exception as error:
                log.write(f"\nFAIL: {error}\n")
                raise
            log.write("\nPASS: Phase 8/9 console checks.\n")
    print(f"PASS. Evidence: {args.log}")


if __name__ == "__main__":
    main()
