#!/usr/bin/env python3
"""Phase 3 hardware acceptance: board CAN <-> a real, independent CAN node.

Needs the board's UART0 console port (pyserial) and a separate USB-CAN adapter
on the same bus (python-can, e.g. --interface slcan --channel /dev/cu.usbmodemXXXX
or --interface socketcan --channel can0). Bus: 2 x 120 ohm termination only at the
two physical ends. See docs/phase-3-can.md. Saves a transcript (suggested: docs/test-reports/can-<baud>-hardware.log).
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
                if "can_error=" in result:
                    raise RuntimeError(result)
                if re.search(pattern, result):
                    return result
        raise TimeoutError(f"No {pattern!r} response to {command!r}: {result[-1000:]}")

    def stats(self):
        response = self.command("can status", r"can_status_end")
        return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", response)}

    def expect_rx(self, frame_text):
        response = self.command("can read", r"can_rx[^\r\n]*\r?\n")
        match = re.search(r"can_rx frame=(\S+)", response)
        if not match or match[1].upper() != frame_text.upper():
            raise AssertionError(f"Expected RX {frame_text}; got {response!r}")


def text_to_msg(can, text):
    ident, _, payload = text.partition("#")
    extended = len(ident) == 8
    if payload.upper() == "R":
        return can.Message(arbitration_id=int(ident, 16), is_extended_id=extended,
                           is_remote_frame=True, dlc=0)
    return can.Message(arbitration_id=int(ident, 16), is_extended_id=extended,
                       data=bytes.fromhex(payload))


def msg_to_text(msg):
    width = 8 if msg.is_extended_id else 3
    tail = "R" if msg.is_remote_frame else bytes(msg.data).hex().upper()
    return f"{msg.arbitration_id:0{width}X}#{tail}"


def require_equal(actual, expected, label):
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def run(console, bus, can, baud):
    console.command("reboot", r"ProtoScope Phase 1 foundation ready", timeout=20)
    time.sleep(2)
    status = console.command("status", r"can_status_end")
    for expected in ("can_active=no", "psram_initialized: yes"):
        if expected not in status:
            raise AssertionError(f"Foundation check missing: {expected}")
    console.command(f"can start {baud} normal", rf"can_start=ok bit_rate={baud}")
    time.sleep(0.5)

    def inject(text):
        bus.send(text_to_msg(can, text))
        time.sleep(0.05)

    # Node -> board: standard, extended, empty, remote, full 8 bytes.
    for text in ("123#0105FF", "1F334455#11", "7FF#", "100#R", "0ABCDEF1#0011223344556677"):
        inject(text)
        console.expect_rx(text)

    # Board -> node: ACKed by the real node, so tx_completed must rise.
    before = console.stats()
    for text in ("201#102AFF", "1F334455#AABB", "123#"):
        console.command(f"can send {text}", rf"can_tx=queued frame={re.escape(text)}")
        got = bus.recv(timeout=2)
        if got is None:
            raise AssertionError(f"Node did not receive {text}")
        require_equal(msg_to_text(got), text.upper(), "board TX frame")
    time.sleep(0.2)
    after = console.stats()
    require_equal(after["tx_completed"] - before["tx_completed"], 3, "tx_completed")

    # Queue pressure: 20 quick frames, 16-frame app queue -> 4 explicit drops.
    before = console.stats()
    for _ in range(20):
        bus.send(text_to_msg(can, "222#01"))
    time.sleep(0.5)
    after = console.stats()
    require_equal(after["rx_frames"] - before["rx_frames"], 20, "burst frame count")
    require_equal(after["rx_queue_drops"] - before["rx_queue_drops"], 4, "queue drops")
    for _ in range(16):
        console.expect_rx("222#01")
    console.command("can read", r"can_rx=empty")

    final = console.stats()
    for key in ("tx_failed", "rx_missed", "rx_overrun", "bus_off", "tx_rejected"):
        require_equal(final[key], 0, key)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console-port", required=True)
    parser.add_argument("--interface", required=True, help="python-can interface, e.g. slcan")
    parser.add_argument("--channel", required=True)
    parser.add_argument("--baud", type=int, choices=(125000, 250000, 500000, 1000000),
                        default=500000)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    import can
    import serial

    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write(f"UTC test start: {datetime.now(timezone.utc).isoformat()}\n")
        log.write(f"Console: {args.console_port}; CAN: {args.interface}/{args.channel} "
                  f"@ {args.baud}\n")
        with serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2) as board, \
             can.Bus(interface=args.interface, channel=args.channel, bitrate=args.baud) as bus:
            board.dtr = board.rts = False
            board.port = args.console_port
            board.open()
            time.sleep(2)
            try:
                run(Console(board, log), bus, can, args.baud)
            except Exception as error:
                log.write(f"\nFAIL: {error}\n")
                raise
            log.write("\nPASS: CAN TX/RX with a real node, IDs, remote/empty frames, queue pressure.\n")
    print(f"PASS at {args.baud} bit/s. Evidence: {args.log}")


if __name__ == "__main__":
    main()
