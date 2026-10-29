"""Capture the DeskBuddy panel framebuffer LIVE (no board reset).

Unlike capture_panel.py (which resets the ESP32 and captures the placeholder
at boot), this script:
  1. launches a generation on llama-server so the card is in its WORKING state,
  2. waits until the device's own poll stream reports 'WORKING',
  3. sends "CAP" over USB-CDC to make the firmware re-stream the CURRENT
     framebuffer (a live generation keeps running the whole time),
  4. composites the 480x480 image and saves a PNG.

Usage: python capture_live.py [PORT] [OUT]
Requires: pyserial, Pillow.
"""
import sys, re, time, base64, struct, json, threading
import urllib.request
import serial
from PIL import Image

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
OUT  = sys.argv[2] if len(sys.argv) > 2 else "panel_live.png"
W = H = 480
SERVER = "http://m.tower1:11444"

tile_re = re.compile(rb"^TILE (\d+) (\d+) (\d+) (\d+)$")


def gen():
    req = urllib.request.Request(
        SERVER + "/v1/chat/completions",
        data=json.dumps({"model": "default", "max_tokens": 4000, "stream": True,
            "messages": [{"role": "user", "content":
                "Write a 3000 word essay with at least 10 numbered sections, each "
                "several paragraphs long, on the complete history of lighthouse "
                "technology from ancient fires to modern automated LED beacons. "
                "Be thorough and do not stop early."}]}).encode(),
        headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            r.read()
    except Exception as e:
        print(f"[gen] {e!r}", flush=True)


def read_stream(s, img, deadline):
    """Consume one TILE...END stream into img. Returns tile count."""
    buf = b""
    tile = None
    tiles = 0
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
                raw = bytes(tile[5])[:tile[4]]
                px = struct.unpack(f"<{tile[2]*tile[3]}H", raw)
                rgb = bytearray()
                for p in px:
                    rgb += bytes((((p >> 11) & 31) * 255 // 31,
                                  ((p >> 5) & 63) * 255 // 63,
                                  (p & 31) * 255 // 31))
                img.paste(Image.frombytes("RGB", (tile[2], tile[3]), bytes(rgb)),
                          (tile[0], tile[1]))
                tiles += 1
                tile = None
    return tiles


def main():
    t = threading.Thread(target=gen)
    t.start()

    s = serial.Serial(PORT, 115200, timeout=1)
    s.reset_input_buffer()
    time.sleep(1.0)

    # wait until the device itself reports WORKING in its poll stream
    print("waiting for device to report WORKING ...", flush=True)
    buf = b""
    working = False
    t0 = time.time()
    while time.time() - t0 < 60 and not working:
        d = s.read(s.in_waiting or 1)
        if d:
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if line.strip().startswith(b"poll: WORKING"):
                    working = True
                    print(line.decode().strip(), flush=True)
                    break
        time.sleep(0.05)
    if not working:
        print("ERROR: device never reached WORKING state", flush=True)
        s.close()
        sys.exit(1)

    # give it one more poll so the rate label is fresh, then trigger capture
    time.sleep(5.2)
    s.write(b"CAP\n")
    print("sent CAP -> streaming live framebuffer", flush=True)

    img = Image.new("RGB", (W, H), (0, 0, 0))
    tiles = read_stream(s, img, time.time() + 30)
    s.close()
    if tiles == 0:
        print("ERROR: no tiles received", flush=True)
        sys.exit(1)
    img.save(OUT)
    print(f"OK: {tiles} tiles -> {OUT} ({W}x{H})  [gen alive={t.is_alive()}]")


if __name__ == "__main__":
    main()
