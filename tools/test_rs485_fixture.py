#!/usr/bin/env python3
"""Phase 4 hardware acceptance: board RS-485 <-> a real, independent RS-485 node.

Needs the board's UART0 console port and a USB-RS485 adapter (a second node) on
the same A/B bus, both via pyserial. Bus: A-A, B-B, common ground reference,
termination only at the two physical ends. Never connect an adapter's power
pin to a GPIO. See docs/phase-4-rs485.md. Saves a transcript.
"""

import argparse
import re
import time
from datetime import datetime, timezone
from pathlib import Path


class Console:
    def __init__(self, port, log):
        self.port, self.log = port, log

    def command(self, command, pattern, timeout=5):
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
                if "rs485_error=" in result:
                    raise RuntimeError(result)
                if re.search(pattern, result):
                    return result
        raise TimeoutError(f"No {pattern!r} response to {command!r}: {result[-1000:]}")

    def stats(self):
        response = self.command("rs485 status", r"rs485_status_end")
        return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", response)}

    def receive(self, expected):
        response = self.command("rs485 read", r"rs485_rx[^\r\n]*\r?\n")
        match = re.search(r"rs485_rx bytes=(\d+) hex=([0-9A-F]+)", response)
        if not match or int(match[1]) != len(expected) or bytes.fromhex(match[2]) != expected:
            raise AssertionError(f"Expected RX {expected!r}; got {response!r}")


def require_equal(actual, expected, label):
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def run(console, node, baud, log):
    console.command("reboot", r"ProtoScope Phase 1 foundation ready", timeout=20)
    time.sleep(2)
    status = console.command("status", r"rs485_status_end")
    for expected in ("psram_initialized: yes", "psram_size_bytes: 8388608", "rs485_active=no"):
        if expected not in status:
            raise AssertionError(f"Foundation check missing: {expected}")
    console.command(f"rs485 start {baud}", rf"rs485_start=ok baud={baud}")
    node.reset_input_buffer()

    def inject(data):
        node.write(data)
        node.flush()
        time.sleep(len(data) * 10 / baud + 0.1)

    # Node -> board: chunking and back-to-back frames keep every wire byte.
    inject(b"HEL")
    inject(b"LO\r\n")
    console.receive(b"HELLO\r\n")
    inject(b"ONE\nTWO\n")
    console.receive(b"ONE\n")
    console.receive(b"TWO\n")

    # Board -> node, with half-duplex turnaround handled by the module.
    for payload in (b"PING\n", b"A" * 63 + b"\n"):
        node.reset_input_buffer()
        console.command(f"rs485 send {payload.hex().upper()}", r"rs485_tx=ok")
        got = node.read_until(b"\n", size=len(payload) + 8)
        require_equal(got, payload, "board TX frame")
    stats = console.stats()
    require_equal(stats["tx_completed"], 2, "tx_completed")

    # Request/response turnaround: board transmits, node answers immediately.
    node.reset_input_buffer()
    console.command("rs485 send 5155455259 0A".replace(" ", ""), r"rs485_tx=ok")  # QUERY\n
    node.read_until(b"\n")
    inject(b"REPLY\n")
    console.receive(b"REPLY\n")

    # Malformed and overlong input is counted, and framing recovers.
    before = console.stats()
    inject(b"\n")
    inject(b"B" * 70 + b"\n")
    inject(b"OK\n")
    console.receive(b"OK\n")
    after = console.stats()
    if after["malformed"] <= before["malformed"] or after["overlong"] <= before["overlong"]:
        raise AssertionError("Malformed/overlong input was not counted")

    final = console.stats()
    for key in ("frame_errors", "parity_errors", "fifo_overflows", "buffer_full", "tx_failures"):
        require_equal(final[key], 0, key)
    log.write(f"\nMeasured echo_bytes after TX = {final['echo_bytes']} "
              "(0 => module does not echo; >0 => module echoes TX to RX).\n")
    log.write(f"Guard window: {final['guard_us']} us at {baud} baud.\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console-port", required=True)
    parser.add_argument("--node-port", required=True, help="USB-RS485 adapter port")
    parser.add_argument("--baud", type=int, choices=(9600, 19200, 38400, 57600, 115200),
                        default=9600)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    import serial

    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write(f"UTC test start: {datetime.now(timezone.utc).isoformat()}\n")
        log.write(f"Console: {args.console_port}; RS-485 node: {args.node_port} @ {args.baud} 8N1\n")
        with serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2) as board, \
             serial.Serial(args.node_port, baudrate=args.baud, timeout=2, write_timeout=2) as node:
            board.dtr = board.rts = False
            board.port = args.console_port
            board.open()
            time.sleep(2)
            try:
                run(Console(board, log), node, args.baud, log)
            except Exception as error:
                log.write(f"\nFAIL: {error}\n")
                raise
            log.write("\nPASS: RS-485 TX/RX with a real node, framing, turnaround, error counters.\n")
    print(f"PASS at {args.baud} baud. Evidence: {args.log}")


if __name__ == "__main__":
    main()
