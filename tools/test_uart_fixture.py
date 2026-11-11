#!/usr/bin/env python3
"""Phase 2 hardware acceptance using separate console and 3.3 V UART ports.

Reboots the board, starts UART1, and saves a transcript. Requires pyserial.
Never connect an adapter's power pin; see docs/phase-2-uart.md.
"""

import argparse
import re
import time
from datetime import datetime, timezone
from pathlib import Path


class Console:
    def __init__(self, port, log):
        self.port = port
        self.log = log

    def command(self, command, pattern, timeout=5):
        self.port.reset_input_buffer()
        self.log.write(f"\n> {command}\n")
        self.log.flush()
        self.port.write((command + "\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        result = ""
        while time.monotonic() < deadline:
            chunk = self.port.read(1024).decode("utf-8", errors="replace")
            if chunk:
                self.log.write(chunk)
                self.log.flush()
                result += chunk
                # Answer the REPL's terminal capability probe if it appears.
                if "\x1b[6n" in chunk:
                    self.port.write(b"\x1b[1;1R")
                if "uart_error=" in result:
                    raise RuntimeError(result)
                if re.search(pattern, result):
                    return result
        raise TimeoutError(f"No {pattern!r} response to {command!r}: {result[-1000:]}")

    def stats(self):
        response = self.command("uart status", r"uart_status_end")
        return {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", response)}

    def receive(self, expected):
        response = self.command("uart read", r"uart_rx[^\r\n]*\r?\n")
        match = re.search(r"uart_rx bytes=(\d+) hex=([0-9A-F]+)", response)
        if not match or int(match[1]) != len(expected) or bytes.fromhex(match[2]) != expected:
            raise AssertionError(f"Expected RX {expected!r}; got {response!r}")


def require_equal(actual, expected, label):
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def run(console, fixture, baud):
    console.command("reboot", r"ProtoScope Phase 1 foundation ready", timeout=20)
    # Let the REPL finish its terminal probe before sending the first command.
    time.sleep(2)
    status = console.command("status", r"uart_status_end")
    for expected in ("psram_initialized: yes", "psram_size_bytes: 8388608", "uart_active=no"):
        if expected not in status:
            raise AssertionError(f"Foundation check missing: {expected}")
    console.command(f"uart start {baud}", rf"uart_start=ok baud={baud}")
    fixture.reset_input_buffer()

    def inject(data):
        fixture.write(data)
        fixture.flush()
        # Includes transmission time at 9600 baud plus RX scheduling allowance.
        time.sleep(len(data) * 10 / baud + 0.08)

    # External adapter -> board; chunking and consecutive frames preserve raw bytes.
    inject(b"HEL")
    inject(b"LO\r\n")
    console.receive(b"HELLO\r\n")
    inject(b"ONE\nTWO\n")
    console.receive(b"ONE\n")
    console.receive(b"TWO\n")
    inject(b"A" * 63 + b"\n")
    console.receive(b"A" * 63 + b"\n")

    # Board -> adapter, exact raw TX including LF and a full 64-byte payload.
    for data in (b"PING\n", b"B" * 63 + b"\n"):
        console.command(f"uart send {data.hex()}", rf"uart_tx=ok bytes={len(data)}")
        require_equal(fixture.read(len(data)), data, "external TX bytes")

    before = console.stats()
    inject(b"A" * 64 + b"\n")
    inject(b"bad\x00line\n")
    inject(b"incomplete")
    time.sleep(0.3)
    require_equal("uart_rx=empty" in console.command("uart read", r"uart_rx=empty"), True,
                  "rejected frames are not delivered")
    after = console.stats()
    for key in ("overlong", "malformed", "partial_timeouts"):
        require_equal(after[key] - before[key], 1, key)
    inject(b"RECOVERED\n")
    console.receive(b"RECOVERED\n")

    # Fill the 16-frame queue while the console does other work, then overflow it.
    before = console.stats()
    inject(b"Q\n" * 20)
    after = console.stats()
    require_equal(after["rx_frames"] - before["rx_frames"], 20, "burst frame count")
    require_equal(after["rx_queue_drops"] - before["rx_queue_drops"], 4, "explicit queue drops")
    for _ in range(16):
        console.receive(b"Q\n")
    console.command("uart read", r"uart_rx=empty")

    # Repeated bidirectional traffic while the supervisor and console run.
    for index in range(100):
        data = f"SEQ{index:03d}\n".encode("ascii")
        inject(data)
        console.receive(data)
        console.command(f"uart send {data.hex()}", rf"uart_tx=ok bytes={len(data)}")
        require_equal(fixture.read(len(data)), data, f"TX sequence {index}")
    final = console.stats()
    require_equal(final["rx_queue_drops"], 4, "total intentional queue drops")
    for key in ("fifo_overflows", "buffer_full", "frame_errors", "parity_errors", "breaks", "tx_failures"):
        require_equal(final[key], 0, key)
    require_equal(final["tx_completed"], 102, "completed transmissions")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console-port", required=True, help="Board UART0 programming/debug port")
    parser.add_argument("--uart-port", required=True, help="Separate 3.3 V adapter wired to GPIO17/18")
    parser.add_argument("--baud", type=int, choices=(9600, 115200), default=115200)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    # macOS cu/tty names for the same port must not be opened as two fixtures.
    normalize = lambda value: str(Path(value).resolve()).replace("/dev/tty.", "/dev/cu.")
    if normalize(args.console_port) == normalize(args.uart_port):
        parser.error("Console and external UART must be different physical serial ports.")
    import serial

    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write(f"UTC test start: {datetime.now(timezone.utc).isoformat()}\n")
        log.write(f"Console: {args.console_port}; fixture: {args.uart_port}; UART: {args.baud} 8N1\n")
        # Assign control lines before open to avoid intentionally toggling EN/BOOT.
        with serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2) as board, \
             serial.Serial(port=None, baudrate=args.baud, timeout=2, write_timeout=2) as fixture:
            board.dtr = board.rts = False
            fixture.dtr = fixture.rts = False
            board.port, fixture.port = args.console_port, args.uart_port
            board.open()
            fixture.open()
            time.sleep(2)
            try:
                run(Console(board, log), fixture, args.baud)
            except Exception as error:
                log.write(f"\nFAIL: {error}\n")
                raise
            log.write("\nPASS: foundation, external UART TX/RX, framing, recovery and queue-pressure tests.\n")
    print(f"PASS at {args.baud} baud. Evidence: {args.log}")


if __name__ == "__main__":
    main()
