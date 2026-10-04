#!/usr/bin/env python3
"""Render the DeskBuddy scene bitmaps for BOTH targets (widget + device).

The "Plumber's run" level (80x64 grid) is authored once here at grid
resolution. Every state is rasterised to:

  bitmaps/widget/<state>.png   320x320 RGB   (4 px/cell)
  bitmaps/widget/scene_assets.rs  const u8 RGBA (A=0xFF) the widget embeds
  bitmaps/device/<state>.png   480x480 RGB   (6 px/cell, full-bleed square)
  bitmaps/device/<state>.bin   480x480 RGB565 LE (= LVGL lv_color_t order)

States:
  working    Phase::Generating  frame 0: sprint, ground, pipe (no coins)
  prompting  Phase::Prompting   SAME picture as working (reused; no
              distinct "thinking" art)
  idle       Phase::Idle        calm tempo, SCENE ONLY (no plumber)
  down       Phase::Down        calm level + a Piranha Plant in the middle
             of the scene — no plumber, no coins

`esp32c6_480x480/firmware/plumber_scene.h` is also (re)generated: four
480x480 RGB565 arrays (480*480*2 = 460800 B each, ~1.8 MB total rodata —
full-bleed square picture on the 480x480 AMOLED panel; the device is
built with the max_app_4MB partition scheme to fit).

Run:  python shared/scene-assets/render_scene.py   (needs Pillow)
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

GRID_W, GRID_H = 80, 80
GY = 72
WIDGET_CELL = 4          # 320x320
DEVICE_CELL = 6          # 480x480

# --- palette: day states (the classic Plumber's run palette) ---------------
SKY_D = (142, 214, 240); SUN = (255, 224, 102); CLOUD = (255, 255, 255)
DIRT = (201, 138, 78); DIRT2 = (179, 118, 63); GRASS = (63, 160, 76)
QB = (243, 194, 44)
PIPE = (46, 158, 87); PIPE_HI = (70, 192, 111); PIPE_DK = (28, 110, 59)
COIN = (255, 216, 74); CAP = (225, 48, 48); CAP_DK = (158, 31, 31)
SKIN = (255, 201, 163); SKIN_DK = (232, 168, 120)
SHIRT = (31, 95, 214); SHIRT_DK = (23, 67, 143); OVER = (36, 85, 184)
BOOT = (90, 58, 28); EYE = (34, 34, 34)
SKY_MID = (133, 207, 234); SKY_HOR = (122, 198, 226)  # banded sky depth
HILL = (124, 172, 104); SHADOW = (140, 92, 52)        # parallax hills, contact shadow
CLOUD_SHADE = (206, 222, 235)                          # shaded cloud underside


class G:
    """An 80x64 grid of (r,g,b)."""
    def __init__(self):
        self.d = [[(0, 0, 0)] * GRID_W for _ in range(GRID_H)]

    def rect(self, x, y, w, h, color):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if 0 <= xx < GRID_W and 0 <= yy < GRID_H:
                    self.d[yy][xx] = color

    def px(self, x, y, color):
        self.rect(x, y, 1, 1, color)

    def circle(self, cx, cy, r, color):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= r * r + r // 2:
                    self.px(cx + dx, cy + dy, color)

    def cloud(self, x, y, color, shade=CLOUD_SHADE):
        self.rect(x, y, 8, 2, color)
        self.rect(x + 1, y - 1, 5, 1, color)
        self.rect(x + 2, y + 2, 5, 1, shade)   # shaded underside -> volume

    def coin(self, x, y, color):
        for j in range(3):
            self.px(x, y + j, color)

    def piranha(self, cx):
        # Classic SMB Piranha Plant (Pakun Flower): a red globe head with
        # white spots and a wide open, fang-lined maw, on a leafy green
        # stalk rooted in the ground. `cx` = center column of the scene.
        # Drawn last, so it reads as the foreground focal point.
        head_r = 7
        head_cy = GY - 13
        # green stalk + fanned leaves at the base (rooted in the ground)
        self.rect(cx - 1, head_cy + 6, 3, GY - (head_cy + 6), PIPE)
        self.rect(cx - 5, GY - 3, 4, 2, PIPE)   # left leaf
        self.rect(cx + 2, GY - 5, 4, 2, PIPE)   # right leaf
        # red globe with a dark rim
        self.circle(cx, head_cy, head_r, CAP_DK)
        self.circle(cx, head_cy, head_r - 1, CAP)
        # white spots on the crown
        self.rect(cx - 4, head_cy - 5, 2, 1, CLOUD)
        self.rect(cx + 2, head_cy - 4, 2, 1, CLOUD)
        self.rect(cx - 1, head_cy - 6, 2, 1, CLOUD)
        # open maw lined with white fangs
        self.rect(cx - 5, head_cy - 2, 10, 4, (90, 15, 15))
        for tx in (cx - 4, cx - 2, cx, cx + 2):      # upper fangs
            self.rect(tx, head_cy - 2, 1, 2, CLOUD)
        for tx in (cx - 3, cx - 1, cx + 1, cx + 3):  # lower fangs
            self.rect(tx, head_cy, 1, 2, CLOUD)

    def plumber(self, x, y, f, running, blink_open, scale=2):
        # The plumber is a 24x27 character map (finer than the old 16x20
        # rect-built sprite -> less chunky, more 8-bit detail) blitted
        # nearest-neighbour at x`scale`. At scale=2 it fills 48x54 cells, and
        # with the anchor at y=20 the planted FRONT foot lands exactly on
        # the grass line (GY). The pose is a RELAXED STANDING figure -- the
        # plumber is NOT running: weight loose, arms at the sides, both feet
        # flat on the ground and set slightly apart (one a touch forward), so
        # he reads as a casual idle, not a stride.
        MU = (110, 66, 30)
        CP = {
            "R": CAP, "r": CAP_DK, "W": CLOUD, "S": SKIN, "s": SKIN_DK,
            "E": EYE, "M": MU, "N": SKIN, "L": OVER, "l": SHIRT, "d": SHIRT_DK,
            "Y": QB, "B": BOOT,
        }
        # 24-wide x 27-tall map; parsed + width-asserted so the columns can
        # never go ragged again (a past bug mis-typed 24-char rows).
        _MAP = """............RR..........
...........RRRR.........
........RRRRRRRR........
.......RRRRRRRRRRR......
.......RRWWWWWWRRR......
.......RRWWrrWWRRR......
.......RRWWWWWWRRR......
........RRRRRRRRRR......
.......RRRRRRRRRRRRRR...
........SSSSSSSSSS......
........SSSESSESSS......
........SSSSNNSSSS......
........SMMMMMMMMM......
.........MMMMMMMM.......
.........RRRRRRRRR......
........RRLLLLLLRR......
........RRLLLLLLRR......
.......RRRLLYYLLRR......
.......RRRLLLLLLRR......
.........LLL.LLL........
.........LLL.LLL........
.........LLL.LLL........
.........LLL.LLL........
.........LLL.LLL........
.........LLL.LLL........
........LLLL.LLLL.......
........LLLL.LLLL.......
"""
        for yy, row in enumerate(_MAP.splitlines()):
            assert len(row) == 24, f"plumber row {yy} = {len(row)} (need 24)"
            for xx, ch in enumerate(row):
                if ch == ".":
                    continue
                self.rect(x + xx * scale, y + yy * scale,
                          scale, scale, CP[ch])


# --- shared level parts ------------------------------------------------------
def level_static(g):
    g.rect(8, GY - 12, 9, 12, PIPE)
    g.rect(8, GY - 12, 2, 12, PIPE_HI)
    g.rect(15, GY - 12, 2, 12, PIPE_DK)
    g.rect(7, GY - 14, 11, 3, PIPE)
    g.rect(7, GY - 14, 2, 3, PIPE_HI)
    g.rect(15, GY - 14, 3, 3, PIPE_DK)


def ground(g):
    g.rect(0, GY, GRID_W, GRID_H - GY, DIRT)
    for x in range(GRID_W):
        g.px(x, GY, GRASS)


def ground_ticks(g, scroll, color):
    for x in range(-scroll, GRID_W - scroll):
        if x % 3 == 0:
            g.rect(x, GY + 3, 1, 2, color)


def sky_bands(g):
    # Banded sky fade (no gradient/AA): two deeper horizontals toward the
    # horizon add depth to the flat sky. Drawn right after the base sky fill,
    # before sun/clouds so they stay in front.
    g.rect(0, GY - 18, GRID_W, 9, SKY_MID)
    g.rect(0, GY - 9, GRID_W, 9, SKY_HOR)


def hills(g):
    # Distant muted-green hill line at the horizon — a single parallax
    # background layer that breaks the empty sky and reads as depth. Drawn
    # before level_static + ground so the pipe sits in front.
    g.rect(0, GY - 2, GRID_W, 2, HILL)
    g.circle(18, GY - 2, 5, HILL)
    g.circle(48, GY - 3, 6, HILL)
    g.circle(70, GY - 2, 4, HILL)


def shadow(g, x, w):
    # 1-cell hard contact shadow on the ground, anchoring an object to the
    # grass line (the NES "grounding shadow" — dark, flat, no AA). Two rows,
    # the lower one inset, so it reads as a flat ellipse under the feet.
    g.rect(x, GY + 1, w, 1, SHADOW)
    g.rect(x + 1, GY + 2, max(1, w - 2), 1, SHADOW)


def coin_row(g):
    # No coins — the coin row was removed from the level (kept as a no-op so
    # the state fns below read cleanly).
    pass


# --- the four states (frame 0, matching the device frames of record) --------
def state_working():
    g = G()
    g.rect(0, 0, GRID_W, GY, SKY_D)
    sky_bands(g)
    g.circle(10, 16, 4, SUN)
    g.cloud(16, 30, CLOUD)
    g.cloud(60, 22, CLOUD)
    g.cloud(40, 8, CLOUD)
    hills(g)
    level_static(g)
    ground(g)
    ground_ticks(g, 0, DIRT2)
    shadow(g, 8, 8)            # under the pipe
    coin_row(g)
    g.plumber(32, 20, 0, True, True, scale=2)
    shadow(g, 44, 24)          # under the plumber's boots
    return g


def state_prompting():
    # Prompting = the SAME picture as working (the user dropped the distinct
    # prompting art — no separate "thinking" sparkle; the running plumber +
    # pipe scene is reused as-is).
    return state_working()


def idle_level(g):
    """The day level with the calm/idle props (blocks unlit) — the scenery
    that BOTH idle and down share. Scene only: no character, no coin row
    (the coins are the "work" signal, so they only appear while a server is
    busy)."""
    g.rect(0, 0, GRID_W, GY, SKY_D)
    sky_bands(g)
    g.circle(10, 8, 4, SUN)
    g.cloud(16, 18, CLOUD)
    g.cloud(60, 10, CLOUD)
    hills(g)
    level_static(g)
    ground(g)
    ground_ticks(g, 0, DIRT2)
    shadow(g, 8, 8)            # under the pipe
    return g


def state_idle():
    # Idle = SCENE ONLY (no plumber, no Z's): the calm level itself.
    return idle_level(G())


def state_down():
    # Down = calm level + a Piranha Plant roaring up in the middle of the
    # scene (classic SMB Pakun Flower: red spotted head, open fanged maw,
    # green stalk). No plumber, no coins.
    g = idle_level(G())
    shadow(g, 34, 12)          # under the Piranha Plant
    g.piranha(40)
    return g


STATES = {
    "working": state_working,
    "prompting": state_prompting,
    "idle": state_idle,
    "down": state_down,
}


# --- output -----------------------------------------------------------------
def upscale(g, cell):
    """Grid -> (w, h, rgb bytes). One grid row = `cell` output rows."""
    out = bytearray()
    for y in range(GRID_H):
        strip = bytearray()
        for color in g.d[y]:
            strip += bytes(color) * cell
        for _ in range(cell):
            out += strip
    return GRID_W * cell, GRID_H * cell, bytes(out)


def to_rgb565(rgb: bytes) -> bytes:
    out = bytearray(len(rgb) // 3 * 2)
    for i in range(len(rgb) // 3):
        r = rgb[i * 3]; gg = rgb[i * 3 + 1]; b = rgb[i * 3 + 2]
        v = ((r & 0xF8) << 8) | ((gg & 0xFC) << 3) | (b >> 3)
        out[2 * i] = v & 0xFF
        out[2 * i + 1] = v >> 8
    return bytes(out)


def emit_c(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] __attribute__((aligned(16))) = {{"]
    for i in range(0, len(data), 16):
        lines.append(",".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def emit_rs(name, rgb: bytes, w, h):
    """RGB -> u8 RGBA (A=0xFF) const array (egui from_rgba_unmultiplied)."""
    n = len(rgb) // 3
    assert n == w * h, f"{name}: {n} px != {w}x{h}"
    vals = []
    for i in range(n):
        r = rgb[i * 3]; g = rgb[i * 3 + 1]; b = rgb[i * 3 + 2]
        vals += [r, g, b, 0xFF]
    lines = [f"pub const {name}: [u8; {len(vals)}] = ["]
    for i in range(0, len(vals), 16):
        lines.append(", ".join(str(v) for v in vals[i:i + 16]) + ",")
    lines.append("];")
    return "\n".join(lines)


def main():
    os.makedirs(os.path.join(HERE, "bitmaps", "widget"), exist_ok=True)
    os.makedirs(os.path.join(HERE, "bitmaps", "device"), exist_ok=True)

    rs_parts = [
        "// AUTO-GENERATED by shared/scene-assets/render_scene.py — do not edit.",
        "// Scene bitmaps for the WIDGET: 320x320 RGBA8 (80x80 grid @ 4 px/cell),",
        "// byte order RGBA, A=0xFF — feed straight to",
        "// egui::ColorImage::from_rgba_unmultiplied([320, 320], &BYTES).",
        "",
        "pub const SCENE_W: usize = 320;",
        "pub const SCENE_H: usize = 320;",
    ]

    hdr = [
        "// AUTO-GENERATED by shared/scene-assets/render_scene.py - do not edit by hand.",
        "// The DeskBuddy scene (see shared/scene-assets/README.md) pre-rendered",
        "// for the 480x480 panel: one FULL-bleed 480x480 square picture per state",
        "// (the 80x80 level at 6 px/cell; the whole panel is scene - sky up top,",
        "// ground at the bottom, no letterbox bands).",
        "// Native RGB565 little-endian (byte-identical to an lv_color_t[] draw",
        "// buffer, so the blit reuses rot90_cw + lv_draw_sw_rgb565_swap).",
        f"// {len(STATES)} states x 480*480*2 = {len(STATES) * 480 * 480 * 2 / (1024 * 1024)} MB rodata",
        "// (fits via the max_app_4MB partition scheme).",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "#define SCENE_W 480",
        "#define SCENE_H 480",
        "#define SCENE_BYTES (SCENE_W * SCENE_H * 2)",
        "",
    ]

    for name, fn in STATES.items():
        g = fn()
        # widget: 320x320
        ww, wh, wrgb = upscale(g, WIDGET_CELL)
        Image.frombytes("RGB", (ww, wh), wrgb).save(
            os.path.join(HERE, "bitmaps", "widget", f"{name}.png"))
        rs_parts.append("")
        rs_parts.append(emit_rs(name.upper(), wrgb, ww, wh))
        # device: 480x480
        dw, dh, drgb = upscale(g, DEVICE_CELL)
        Image.frombytes("RGB", (dw, dh), drgb).save(
            os.path.join(HERE, "bitmaps", "device", f"{name}.png"))
        d565 = to_rgb565(drgb)
        with open(os.path.join(HERE, "bitmaps", "device", f"{name}.bin"), "wb") as f:
            f.write(d565)
        hdr.append(emit_c(f"scene_{name}_data", d565))
        hdr.append("")
        print(f"{name}: widget {ww}x{wh} + device {dw}x{dh} ok")

    with open(os.path.join(HERE, "bitmaps", "widget", "scene_assets.rs"), "w") as f:
        f.write("\n".join(rs_parts) + "\n")
    with open(os.path.join(ROOT, "esp32c6_480x480", "firmware", "plumber_scene.h"), "w") as f:
        f.write("\n".join(hdr) + "\n")
    print("wrote bitmaps/widget/scene_assets.rs + firmware/plumber_scene.h")


if __name__ == "__main__":
    main()
