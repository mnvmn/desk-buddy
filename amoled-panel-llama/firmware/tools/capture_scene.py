"""Capture the plumber-scene states from the DeskBuddy device without a
board reset or a generation: send the scene driver commands (SCN=working,
SCP=prompting, SCI=idle, SDN=down, CAR=card) over USB-CDC,
then CAP to stream the current framebuffer, and composite each into a PNG.

Usage: python capture_scene.py [PORT] [OUT_PREFIX]
Produces: OUT_PREFIX_working.png, OUT_PREFIX_prompting.png, OUT_PREFIX_idle.png,
          OUT_PREFIX_down.png, OUT_PREFIX_card.png
"""
import sys, re, time, base64, struct
import serial
from PIL import Image

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
PREFIX = sys.argv[2] if len(sys.argv) > 2 else "scene"
W = H = 480

tile_re = re.compile(rb"^TILE (\d+) (\d+) (\d+) (\d+)$")


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
            if line.startswith(b"cap: "):
                print(line.decode(errors="replace"), flush=True)
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
            if not re.match(rb"^[A-Za-z0-9+/=]+$", line):
                continue  # stray serial line (poll/scene) mid-stream
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


def capture(s, cmd, out):
    s.reset_input_buffer()
    s.write(cmd)  # e.g. b"SCN\n"
    # Wait for the device's "cmd: XXX" ack so CAP can't land mid-stream or
    # while the screen switch is still in flight (poll lines keep flowing).
    buf = b""
    t0 = time.time()
    while time.time() - t0 < 5:
        chunk = s.read(16384)
        if chunk:
            buf += chunk
        if b"cmd: " in buf:
            break
    s.reset_input_buffer()
    time.sleep(0.4)
    s.write(b"CAP\n")
    print(f"sent {cmd.decode().strip()} -> CAP", flush=True)
    img = Image.new("RGB", (W, H), (0, 0, 0))
    tiles = read_stream(s, img, time.time() + 30)
    if tiles == 0:
        print(f"ERROR: no tiles for {cmd.decode().strip()}", flush=True)
        return False
    img.save(out)
    print(f"OK: {cmd.decode().strip()} -> {out} ({tiles} tiles)", flush=True)
    return True


def main():
    s = serial.Serial(PORT, 115200, timeout=1)
    s.reset_input_buffer()
    time.sleep(1.0)
    ok = True
    ok &= capture(s, b"SCN\n", f"{PREFIX}_working.png")
    ok &= capture(s, b"SCP\n", f"{PREFIX}_prompting.png")
    ok &= capture(s, b"SCI\n", f"{PREFIX}_idle.png")
    ok &= capture(s, b"SDN\n", f"{PREFIX}_down.png")
    ok &= capture(s, b"CAR\n", f"{PREFIX}_card.png")
    s.close()
    if not ok:
        sys.exit(1)
    print("done")


if __name__ == "__main__":
    main()
