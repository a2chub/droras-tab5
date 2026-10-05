#!/usr/bin/env python3
"""Capture the Tab5 screen over USB serial and save it as a PNG.

Usage:
    ~/.platformio/penv/bin/python tools/screenshot.py out.png [--port /dev/cu.usbmodemXXXX]
    ~/.platformio/penv/bin/python tools/screenshot.py out.png --send "tap 1190 28"

--send writes debug console commands (see src/debug_console.h) before capturing.
Needs pyserial, which the PlatformIO virtualenv already provides.
"""

import argparse
import glob
import struct
import sys
import time
import zlib

import serial


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) != 1:
        sys.exit(f"expected exactly one /dev/cu.usbmodem* port, found {ports}; pass --port")
    return ports[0]


def open_port(name):
    port = serial.Serial()
    port.port = name
    port.baudrate = 115200
    port.timeout = 0.5
    # DTR and RTS are deliberately left at pyserial's defaults (both asserted). Clearing them
    # makes them drop one after the other, and the ESP32-P4's USB-Serial/JTAG bridge treats
    # that brief RTS-only state as a reset request (measured: the board reboots on open).
    port.open()
    return port


def read_frame(port, timeout_s):
    """Returns (width, height, rows) where rows is a list of RGB565 value lists."""
    deadline = time.time() + timeout_s
    width = height = None
    rows = []
    buffer = b""
    while time.time() < deadline:
        buffer += port.read(65536)
        *lines, buffer = buffer.split(b"\n")
        for raw in lines:
            line = raw.strip().decode("ascii", "replace")
            if line.startswith("@SHOT "):
                _, w, h = line.split()
                width, height, rows = int(w), int(h), []
            elif line.startswith("@R ") and width is not None:
                data = line[3:]
                row = []
                for i in range(0, len(data), 8):
                    row.extend([int(data[i + 4:i + 8], 16)] * int(data[i:i + 4], 16))
                if len(row) != width:
                    sys.exit(f"row {len(rows)} decoded to {len(row)} pixels, expected {width} (serial data lost?)")
                rows.append(row)
            elif line == "@END" and width is not None:
                if len(rows) != height:
                    sys.exit(f"got {len(rows)} rows, expected {height}")
                return width, height, rows
    sys.exit("timed out waiting for the screenshot")


def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)  # PNG filter type: none
        for value in row:
            r, g, b = (value >> 11) & 0x1F, (value >> 5) & 0x3F, value & 0x1F
            raw += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output")
    parser.add_argument("--port")
    parser.add_argument("--send", action="append", default=[], help="console command to send first (repeatable)")
    parser.add_argument("--settle", type=float, default=0.5, help="seconds to wait after each --send command")
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    port = open_port(args.port or find_port())
    try:
        for command in args.send:
            port.write(command.encode() + b"\n")
            port.flush()
            time.sleep(args.settle)
        port.reset_input_buffer()
        port.write(b"shot\n")
        port.flush()
        width, height, rows = read_frame(port, args.timeout)
    finally:
        port.close()
    write_png(args.output, width, height, rows)
    print(f"saved {args.output} ({width}x{height})")


if __name__ == "__main__":
    main()
