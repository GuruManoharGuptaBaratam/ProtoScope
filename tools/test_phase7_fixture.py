#!/usr/bin/env python3
"""Phase 7 hardware check of the routing engine with the REAL drivers and no external devices.

Wiring (board powered off, remove afterwards):
  jumper GPIO17 <-> GPIO18   UART1 loopback  (its TX is also its RX)
  jumper GPIO4  <-> GPIO5    CAN controller self-test loopback
  jumper GPIO40 <-> GPIO41   SPI MOSI <-> MISO loopback
This exercises RT-01 (UART->CAN), the RT-01/RT-02 loop guard and RT-14 (SPI->UART)
end to end. It does NOT replace device-level tests of the other routes.
Saves a transcript. See docs/phase-7-routing-engine.md.
"""

import argparse
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
                if re.search(r"(route|uart|can|spi|msg)_error=", result):
                    raise RuntimeError(result)
                if re.search(pattern, result):
                    return result
        raise TimeoutError(f"No {pattern!r} response to {command!r}: {result[-1200:]}")

    def route_stats(self, route_id):
        response = self.command(f"route stats {route_id}", r"route_stats_end")
        line = next(l for l in response.splitlines() if l.startswith("route_stats id=RT-"))
        return {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", line)}

    def iface(self, name):
        response = self.command("route stats", r"route_stats_end")
        line = next(l for l in response.splitlines() if f"route_iface proto={name} " in l)
        return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)}


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def send_line(console, text):
    console.command(f"uart send {text.encode().hex().upper()}", r"uart_tx=ok")
    time.sleep(0.5)


def run(console):
    console.command("reboot", r"ProtoScope Phase 1 foundation ready", timeout=20)
    time.sleep(2)
    status = console.command("status", r"route_engine|routes_enabled=\d+")
    for needed in ("psram_initialized: yes", "routes_enabled=0"):
        expect(needed in status, f"Foundation check missing: {needed}")
    listing = console.command("route list", r"route_list_end")
    expect(len(re.findall(r"route_id=RT-\d\d ", listing)) == 20, "route list must show 20 routes")
    expect("route_id=RT-01 src=UART dst=CAN enabled=no config=ready" in listing, "RT-01 default profile")
    expect("route_id=RT-02 src=CAN dst=UART enabled=no config=can_filter_not_set" in listing,
           "RT-02 must stay unconfigured")

    console.command("uart start 115200", r"uart_start=ok")
    console.command("can start 500000 selftest", r"can_start=ok")
    console.command("spi start 0 1000000", r"spi_start=ok")

    # 1. RT-01 end to end: UART line -> decode -> CAN frame (self-received on the CAN controller).
    console.command("route enable 1", r"route_enable=ok")
    send_line(console, "CAN,123,3,0105FF\n")
    polled = console.command("msg poll can", r"msg_rx[^\r\n]*\r?\n")
    expect("proto=CAN" in polled and "data=0105FF" in polled and "id=123 " in polled, f"RT-01 CAN frame: {polled}")
    st = console.route_stats(1)
    expect(st["matched"] == 1 and st["transmitted"] == 1, f"RT-01 counters: {st}")

    # 2. Negative vectors are rejected and counted, nothing is transmitted.
    for bad in ("CAN,800,1,00\n", "CAN,123,3,0105\n", "CAN,XYZ,1,00\n"):
        send_line(console, bad)
    st = console.route_stats(1)
    expect(st["rejected_decode"] == 3 and st["transmitted"] == 1, f"RT-01 rejections: {st}")
    console.command("msg poll can", r"msg_rx=empty")

    # 3. Loop guard: RT-01 + RT-02 form UART->CAN->UART; the CAN echo must not re-enter.
    console.command("route reset", r"route_reset=ok")
    console.command("route set 2 filter=000/000", r"route_set=ok")
    console.command("route enable 2", r"route_enable=ok")
    send_line(console, "CAN,123,3,0105FF\n")
    time.sleep(0.5)
    can = console.iface("CAN")
    st1, st2 = console.route_stats(1), console.route_stats(2)
    expect(st1["transmitted"] == 1, f"RT-01 should send once: {st1}")
    expect(can["loop_suppressed"] == 1 and st2["matched"] == 0, f"loop guard: {can} {st2}")
    console.command("route disable 1", r"route_disable=ok")
    console.command("route disable 2", r"route_disable=ok")

    # 4. RT-14 (SPI -> UART): a scheduled SPI read (MOSI looped to MISO) becomes a UART line.
    console.command("uart read", r"uart_rx=empty|uart_rx bytes")   # clear anything pending
    console.command("route set 14 poll_spi=9F0000", r"route_set=ok")
    console.command("route enable 14", r"route_enable=ok")
    console.command("route trigger 14", r"route_trigger=queued")
    time.sleep(0.5)
    line = console.command("uart read", r"uart_rx bytes=\d+ hex=[0-9A-F]+")
    expect("hex=3946303030300A" in line, f"RT-14 output should be '9F0000\\n': {line}")
    st = console.route_stats(14)
    expect(st["polls"] == 1 and st["transmitted"] == 1 and st["poll_failures"] == 0, f"RT-14 counters: {st}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console-port", required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    import serial

    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write(f"UTC test start: {datetime.now(timezone.utc).isoformat()}\n")
        log.write(f"Console: {args.console_port}; loopbacks: GPIO17-18, GPIO4-5, GPIO40-41\n")
        with serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2) as board:
            board.dtr = board.rts = False
            board.port = args.console_port
            board.open()
            time.sleep(2)
            try:
                run(Console(board, log))
            except Exception as error:
                log.write(f"\nFAIL: {error}\n")
                raise
            log.write("\nPASS: RT-01 end to end, negative vectors, loop guard, RT-14 with real drivers.\n")
    print(f"PASS. Evidence: {args.log}")


if __name__ == "__main__":
    main()
