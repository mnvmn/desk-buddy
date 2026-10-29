"""Capture the DeskBuddy TOKEN CARD (the data card, not the default plumber
scene) in a chosen state, unrotated and upright.

The device boots to the scene screen. 'CAR' switches to the data card; 'CAP'
streams the current framebuffer (480x480 RGB565, rotated 90 deg CW on the
panel). We send CAR, optionally fire a live generation to reach the working
state, send CAP, composite the tiles, and rotate CCW 90 so the output is
upright — ready for a side-by-side comparison against the widget goldens.

Usage:
  python cap_card.py <state> <out.png> [PORT]
    state:  idle | working
    PORT:   COM3 (default)

Requires: pyserial, Pillow.
"""
import sys, re, time, base64, struct, threading, json
import urllib.request
import serial
from PIL import Image

W = H = 480
SERVER = "http://m.tower1:11444"
tile_re = re.compile(rb"^TILE (\d+) (\d+) (\d+) (\d+)$")


def read_stream(s, img, deadline):
    buf = b""; tile = None; tiles = 0; got_end = False
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
                tile = [x, y, w, h, w * h * 2, bytearray()]; continue
            if line == b"END":
                got_end = True; break
            if tile is None or not line:
                continue
            data = base64.b64decode(line + b"=" * ((-len(line)) % 4))
            tile[5] += data
            if len(tile[5]) >= tile[4]:
                raw = bytes(tile[5])[:tile[4]]
                px = struct.unpack(f"<{tile[2]*tile[3]}H", raw)
                rgb = bytearray()
                for p in px:
                    rgb += bytes((((p >> 11) & 31) * 255 // 31,
                                  ((p >> 5) & 63) * 255 // 63,
                                  (p & 31) * 255 // 31))
                img.paste(Image.frombytes("RGB", (tile[2], tile[3]), bytes(rgb)),
                          (tile[0], tile[1]))
                tiles += 1; tile = None
    return tiles


def gen():
    req = urllib.request.Request(SERVER + "/v1/chat/completions",
        data=json.dumps({"model": "default", "max_tokens": 4000, "stream": True,
            "messages": [{"role": "user", "content":
                "Write a 3000 word essay on the history of lighthouse technology "
                "with 10 numbered sections. Be thorough."}]}).encode(),
        headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=200) as r:
            r.read()
    except Exception as e:
        print(f"[gen] {e!r}", flush=True)


def capture(s):
    s.reset_input_buffer()
    s.write(b"CAR\n"); time.sleep(1.2)   # show the data card
    s.write(b"CAP\n")
    img = Image.new("RGB", (W, H), (0, 0, 0))
    tiles = read_stream(s, img, time.time() + 30)
    img = img.rotate(90, expand=True)    # panel is 90 deg CW -> CCW to upright
    return img, tiles


def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    state = sys.argv[1].lower()
    out = sys.argv[2]
    port = sys.argv[3] if len(sys.argv) > 3 else "COM3"

    s = serial.Serial(port, 115200, timeout=1)
    s.reset_input_buffer()
    time.sleep(1.0)

    gt = None
    if state == "working":
        gt = threading.Thread(target=gen); gt.start()
        time.sleep(2.0)
        buf = b""; working = False; t0 = time.time()
        while time.time() - t0 < 70 and not working:
            d = s.read(s.in_waiting or 1)
            if d:
                buf += d
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    print("  ", line.decode().strip(), flush=True)
                    if b"WORKING" in line:
                        working = True; break
            time.sleep(0.05)
        if not working:
            print("ERROR: device never reached WORKING", flush=True)
        time.sleep(5.0)   # let the rate settle

    img, tiles = capture(s)
    s.close()
    if tiles == 0:
        print("ERROR: no tiles received"); sys.exit(1)
    img.save(out)
    print(f"OK: {state} card, {tiles} tiles -> {out} (480x480 upright)"
          + (f"  [gen alive={gt.is_alive()}]" if gt else ""))


if __name__ == "__main__":
    main()
