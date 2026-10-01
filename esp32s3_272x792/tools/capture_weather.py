#!/usr/bin/env python
"""Capture the live weather framebuffer from the S3 weather_test firmware.

Waits for 'panel up:' on serial, sends 'CAP', collects base64 lines between
CAP_START and CAP_END (272x100 bytes = 800x272 buffer, 1bpp MSB-first rows),
and writes a PNG (e-ink sense: bit set = dark ink).
"""
import base64, sys, time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
OUT = sys.argv[2] if len(sys.argv) > 2 else "weather_capture.png"

W, H = 800, 272
s = serial.Serial(PORT, 115200, timeout=1)

# ---- phase 1: wait for 'panel up:' (boot = wifi + 3 TLS fetches + refresh) --
t0 = time.time()
up = False
while time.time() - t0 < 90:
    try:
        line = s.readline().decode(errors="replace").rstrip()
    except Exception as e:
        print("serial err:", e)
        break
    if line:
        print("  |", line)
    if "panel up:" in line:
        up = True
        break
if not up:
    sys.exit("panel never came up")

# ---- phase 2: request capture ----------------------------------------------
s.write(b"CAP\r\n")
t1 = time.time()
b64 = []
while time.time() - t1 < 120:
    line = s.readline().decode(errors="replace").strip()
    if line == "CAP_START":
        continue
    if line.startswith("CAP_END"):
        print("capture complete")
        break
    if line:
        b64.append(line)

raw = base64.b64decode("".join(b64))
need = W * H // 8
print(f"got {len(raw)} bytes (need {need})")
if len(raw) < need:
    sys.exit("short capture")
raw = raw[:need]

# ---- composite: buffer is rows of 800 bits, MSB-first (byte 0 = leftmost 8)
from PIL import Image
img = Image.new("L", (W, H), 255)
px = img.load()
for y in range(H):
    for x in range(W):
        byte = raw[y * 100 + x // 8]
        if byte & (0x80 >> (x % 8)):
            px[x, y] = 0  # ink

img.save(OUT)
print("wrote", OUT)
