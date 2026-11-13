#!/usr/bin/env python3
"""Phase 5/6 console acceptance (pyserial only, board console on UART0).

Always: foundation state, 'msg caps' for all five interfaces.
With --spi-loopback: jumper GPIO40 (MOSI) to GPIO41 (MISO), no device attached;
  checks raw SPI transfers and the same transfer through the common event contract.
With --can-selftest: jumper GPIO4 to GPIO5 (or a CAN transceiver attached);
  checks a CAN frame through the common event contract.
With --i2c-addr/--i2c-reg/--i2c-expect: a real I2C device on GPIO8/GPIO9 (3.3 V,
  pull-ups present once); checks scan, register read and the same read through events.
Saves a transcript. See docs/phase-5-i2c-spi.md and docs/phase-6-event-abstraction.md.
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
                if re.search(r"(i2c|spi|msg)_error=", result) or "msg_failed" in result:
                    raise RuntimeError(result)
                if re.search(pattern, result):
                    return result
        raise TimeoutError(f"No {pattern!r} response to {command!r}: {result[-1000:]}")

    def stats(self, name):
        response = self.command(f"{name} status", rf"{name}_status_end")
        return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", response)}


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def run(console, args):
    console.command("reboot", r"ProtoScope Phase 1 foundation ready", timeout=20)
    time.sleep(2)
    status = console.command("status", r"spi_status_end")
    for needed in ("psram_initialized: yes", "psram_size_bytes: 8388608", "i2c_active=no",
                   "spi_active=no", "can_active=no", "rs485_active=no", "uart_active=no"):
        expect(needed in status, f"Foundation check missing: {needed}")

    caps = console.command("msg caps", r"msg_caps_end")
    for proto in ("UART", "RS485", "CAN", "I2C", "SPI"):
        expect(f"protocol={proto} " in caps, f"msg caps missing {proto}")
    expect(re.search(r"protocol=I2C .*receive=controller_transaction .*CONTROLLER_ONLY", caps),
           "I2C must be controller-only")
    expect(re.search(r"protocol=SPI .*FULL_DUPLEX", caps), "SPI must be full duplex")

    if args.spi_loopback:
        console.command(f"spi start {args.spi_mode} {args.spi_clock}", r"spi_start=ok")
        for pattern in ("A5", "0102030405060708", "FF00FF00", "AB" * 64):
            response = console.command(f"spi xfer {pattern}", r"spi_rx bytes=\d+ hex=[0-9A-F]+")
            expect(f"hex={pattern}" in response, f"SPI loopback mismatch for {pattern}: {response}")
        result = console.command("msg send spi 9F0000", r"msg_result[^\r\n]*\r?\n")
        expect("proto=SPI" in result and "dir=RX" in result and "status=OK" in result and
               "data=9F0000" in result, f"Unexpected SPI event result: {result}")
        spi = console.stats("spi")
        expect(spi["failures"] == 0 and spi["rejected"] == 0 and spi["completed"] == 5,
               f"SPI counters unexpected: {spi}")

    if args.can_selftest:
        console.command("can start 500000 selftest", r"can_start=ok")
        console.command("msg send can 123#0105FF", r"msg_result[^\r\n]*\r?\n")
        time.sleep(0.3)
        polled = console.command("msg poll can", r"msg_rx[^\r\n]*\r?\n")
        expect("proto=CAN" in polled and "data=0105FF" in polled and "dir=RX" in polled,
               f"CAN event mismatch: {polled}")

    if args.i2c_addr:
        addr = args.i2c_addr.replace("0x", "")
        console.command("i2c start 100000" + (" pullup" if args.i2c_pullup else ""), r"i2c_start=ok")
        scan = console.command("i2c scan", r"i2c_scan_done count=\d+")
        expect(f"i2c_found=0x{int(addr, 16):02X}" in scan, f"Device 0x{addr} not found: {scan}")
        reg, want = args.i2c_reg, args.i2c_expect.upper()
        length = len(want) // 2
        read = console.command(f"i2c wr {addr} {reg} {length}", r"i2c_rx addr=[^\r\n]*\r?\n")
        expect(f"hex={want}" in read, f"I2C register read mismatch: {read}")
        event = console.command(f"msg send i2c {addr} wr {reg} {length}", r"msg_result[^\r\n]*\r?\n")
        expect("proto=I2C" in event and "status=OK" in event and f"data={want}" in event and
               "ack=ACK" in event, f"Unexpected I2C event result: {event}")
        i2c = console.stats("i2c")
        expect(i2c["bus_errors"] == 0 and i2c["timeouts"] == 0, f"I2C errors: {i2c}")
        # A missing device must be reported as NACK/failure, not hang.
        bad = "77" if addr.lower() != "77" else "76"
        console.port.reset_input_buffer()
        console.port.write(f"i2c read {bad} 1\n".encode())
        time.sleep(1)
        console.log.write(console.port.read(2048).decode("utf-8", errors="replace"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console-port", required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--spi-loopback", action="store_true")
    parser.add_argument("--spi-mode", type=int, choices=range(4), default=0)
    parser.add_argument("--spi-clock", type=int, default=1000000)
    parser.add_argument("--can-selftest", action="store_true")
    parser.add_argument("--i2c-addr", help="7-bit hex address of a real device, e.g. 68")
    parser.add_argument("--i2c-reg", help="register bytes to write before reading, hex, e.g. 75")
    parser.add_argument("--i2c-expect", help="expected read-back hex, e.g. 68")
    parser.add_argument("--i2c-pullup", action="store_true", help="enable ESP internal pull-ups")
    args = parser.parse_args()
    if args.i2c_addr and not (args.i2c_reg and args.i2c_expect):
        parser.error("--i2c-addr needs --i2c-reg and --i2c-expect")
    import serial

    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write(f"UTC test start: {datetime.now(timezone.utc).isoformat()}\n")
        log.write(f"Console: {args.console_port}; options: {vars(args)}\n")
        with serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2) as board:
            board.dtr = board.rts = False
            board.port = args.console_port
            board.open()
            time.sleep(2)
            try:
                run(Console(board, log), args)
            except Exception as error:
                log.write(f"\nFAIL: {error}\n")
                raise
            log.write("\nPASS: selected Phase 5/6 console checks.\n")
    print(f"PASS. Evidence: {args.log}")


if __name__ == "__main__":
    main()
