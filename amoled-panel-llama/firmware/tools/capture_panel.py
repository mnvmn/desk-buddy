"""Capture the DeskBuddy panel framebuffer from the ESP32-C6 over USB-CDC.

The SH8601 panel is write-only (no hardware readback), so firmware.ino swaps
the LVGL flush callback at boot, forces a full-screen redraw, and streams each
flushed tile over serial:  "TILE <x> <y> <w> <h>" + base64 lines (RGB565 LE),
until a line "END". This script resets the board (DTR/RTS), waits for the
stream, composites the 480x480 image and saves a PNG.

Usage: python capture_panel.py COM3 out.png
Requires: pyserial, Pillow (pip install pyserial pillow)
"""
import sys
import re
import time
import base64
import struct

import serial
from PIL import Image

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
OUT = sys.argv[2] if len(sys.argv) > 2 else "panel.png"
W = H = 480


def reset(port):
    """DTR/RTS toggle -> normal (booted) reset of the ESP32."""
    s = serial.Serial(port, 115200, timeout=0.05)
    s.dtr = False
    s.rts = False
    time.sleep(0.1)
    s.rts = True
    time.sleep(0.1)
    s.dtr = True
    time.sleep(0.5)
    s.dtr = False
    s.rts = False
    s.reset_input_buffer()
    time.sleep(1.5)  # let the boot log drain
    return s


def main():
    s = reset(PORT)
    img = Image.new("RGB", (W, H), (0, 0, 0))
    tiles = 0
    deadline = time.time() + 90
    tile_re = re.compile(rb"^TILE (\d+) (\d+) (\d+) (\d+)$")
    tile = None  # [x, y, w, h, need_bytes, bytearray]
    buf = b""
    got_end = False
    while time.time() < deadline and not got_end:
        chunk = s.read(16384)
        if chunk:
            buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.strip()
            m = tile_re.match(line)
            if m:
                x, y, w, h = (int(v) for v in m.groups())
                tile = [x, y, w, h, w * h * 2, bytearray()]
                continue
            if line == b"END":
                got_end = True
                break
            if tile is None or not line:
                continue
            data = base64.b64decode(line + b"=" * ((-len(line)) % 4))
            tile[5] += data
            if len(tile[5]) >= tile[4]:
                w, h, raw = tile[2], tile[3], bytes(tile[5])[: tile[4]]
                px = struct.unpack(f"<{w * h}H", raw)
                rgb = bytearray()
                for p in px:
                    r = ((p >> 11) & 31) * 255 // 31
                    g = ((p >> 5) & 63) * 255 // 63
                    b = (p & 31) * 255 // 31
                    rgb += bytes((r, g, b))
                img.paste(Image.frombytes("RGB", (w, h), bytes(rgb)), (tile[0], tile[1]))
                tiles += 1
                tile = None
    s.close()
    if tiles == 0:
        print("ERROR: no tiles received - capture failed")
        sys.exit(1)
    img.save(OUT)
    print(f"OK: {tiles} tiles -> {OUT} ({W}x{H})")


if __name__ == "__main__":
    main()
