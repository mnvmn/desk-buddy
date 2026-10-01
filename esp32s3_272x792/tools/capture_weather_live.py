#!/usr/bin/env python
"""Reset the S3 out of download mode, wait for 'panel up:', send CAP, save PNG."""
import base64, sys, time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
OUT = sys.argv[2] if len(sys.argv) > 2 else "C:/Users/m/AppData/Local/hermes/cache/scratch/weather_capture.png"
W, H = 800, 272

s = serial.Serial(PORT, 115200, timeout=1)
# exit download mode: DTR low, RTS pulse
s.dtr = False
s.rts = True
time.sleep(0.05)
s.rts = False
time.sleep(0.05)
s.dtr = True
s.reset_input_buffer()

t0 = time.time()
up = False
while time.time() - t0 < 120:
    try:
        line = s.readline().decode(errors="replace").rstrip()
    except Exception as e:
        print("serial err:", e); break
    if line:
        print("  |", line)
    if "panel up:" in line:
        up = True
        break
if not up:
    sys.exit("panel never came up")

s.write(b"CAP\r\n")
t1 = time.time()
b64 = []
while time.time() - t1 < 120:
    line = s.readline().decode(errors="replace").strip()
    if line == "CAP_START":
        continue
    if line.startswith("CAP_END"):
        print("capture complete"); break
    if line:
        b64.append(line)

raw = base64.b64decode("".join(b64))
need = W * H // 8
print(f"got {len(raw)} bytes (need {need})")
if len(raw) < need:
    sys.exit("short capture")
raw = raw[:need]
open(OUT + ".bin", "wb").write(raw)  # keep raw bytes for offline conversion

from PIL import Image
img = Image.new("L", (W, H), 255)
px = img.load()
for y in range(H):
    for x in range(W):
        if raw[y * 100 + x // 8] & (0x80 >> (x % 8)):
            px[x, y] = 0
img.save(OUT)
print("wrote", OUT)
