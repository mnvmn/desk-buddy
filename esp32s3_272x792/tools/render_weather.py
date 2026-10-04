#!/usr/bin/env python
"""Render the 12h weather mock as a 792x272 1bpp e-paper bitmap (MSB-first).

Output: fw/test_sketch/weatherimg.h  (const uint8_t gImage_weather[])
Format matches the EDP lib: 1bpp, row-major, MSB-first, 0x80=ink (black),
row width a multiple of 8 (792 ok).
"""
from PIL import Image, ImageDraw

W, H = 792, 272

BG    = (255, 255, 255)
INK   = (24, 26, 30)     # main text
MUT   = (60, 62, 66)     # secondary text (must be < 128 lum to survive 1-bit)
LBL   = (45, 48, 54)     # axis labels (solid black on e-ink)
BLUE  = (24, 26, 30)     # curve — pure black, no AA gap risk on 1-bit
# LBLUE/NIGHT/MOON removed: grays can't exist on 1-bit e-ink.

# ---- 5x7 pixel font -------------------------------------------------------
GLYPHS = {
    '0': ['01110','10001','10011','10101','11001','10001','01110'],
    '1': ['00100','01100','00100','00100','00100','00100','01110'],
    '2': ['01110','10001','00001','00110','01000','10000','11111'],
    '3': ['11110','00001','00001','01110','00001','00001','11110'],
    '4': ['00010','00110','01010','10010','11111','00010','00010'],
    '5': ['11111','10000','11110','00001','00001','10001','01110'],
    '6': ['00110','01000','10000','11110','10001','10001','01110'],
    '7': ['11111','00001','00010','00100','01000','01000','01000'],
    '8': ['01110','10001','10001','01110','10001','10001','01110'],
    '9': ['01110','10001','10001','01111','00001','00110','01001'],
    'F': ['11111','10000','11110','10000','10000','10000','10000'],
    'E': ['11111','10000','10000','11110','10000','10000','11111'],
    'L': ['10000','10000','10000','10000','10000','10000','11111'],
    'S': ['01111','10000','10000','01110','00001','00001','11110'],
    'D': ['11110','10001','10001','10001','10001','10001','11110'],
    'R': ['11110','10001','10001','11110','10100','10010','10001'],
    'Y': ['10001','10001','01010','00100','00100','00100','00100'],
    'H': ['10001','10001','10001','11111','10001','10001','10001'],
    'K': ['10001','10010','10100','11000','10100','10010','10001'],
    'M': ['10001','11011','10101','10101','10001','10001','10001'],
    '.': ['00000','00000','00000','00000','00000','00110','00110'],
    'o': ['00000','01110','10001','10001','10001','10001','01110'],
    '-': ['00000','00000','00000','11111','00000','00000','00000'],
    '°': ['00000','01100','01100','00000','00000','00000','00000'],  # 2x2 dot: compact at any scale
    '/': ['00000','00011','00110','01100','11000','00000','00000'],  # 2-px connected diagonal (reads solid at sc=2)
    ' ': ['00000','00000','00000','00000','00000','00000','00000'],
    '\u2192': ['00000','00010','00110','11111','00110','00010','00000'],
}

def put_text(d, x, y, s, color, sc=2):
    # FAIL LOUDLY on unknown glyphs — the old silent-skip dropped '/' from "8 KM/H"
    missing = {c for c in s if c not in GLYPHS}
    if missing:
        raise SystemExit(f"put_text: glyphs missing from GLYPHS: {missing!r} in {s!r}")
    cx = x
    for ch in s:
        for r, row in enumerate(GLYPHS[ch]):
            for c, bit in enumerate(row):
                if bit == '1':
                    d.rectangle([cx + c*sc, y + r*sc,
                                 cx + c*sc + sc - 1, y + r*sc + sc - 1],
                                fill=color)
        cx += 6 * sc
    return cx

# ---- canvas ---------------------------------------------------------------
img = Image.new('RGB', (W, H), BG)
d = ImageDraw.Draw(img)

# ---- right: temperature curve --------------------------------------------
CX0, CX1, CY_TOP, CY_BOT = 240, 770, 56, 170
temps = [19.7, 19.8, 19.8, 19.1, 17.7, 16.3, 15.3, 14.7, 13.8, 13.4, 12.8, 12.3, 12.1]
pts = []
for i, t in enumerate(temps):
    px = CX0 + i * (CX1 - CX0) // 12
    py = CY_TOP + int((21.0 - t) / 11.0 * (CY_BOT - CY_TOP))
    pts.append((px, py))

# night band: 50% checkerboard dither = light gray on 1-bit e-ink (sunset ~19:45 → last 5 h)
n0, n1 = 612, CX1
for y in range(CY_TOP - 14, CY_BOT + 1):
    for x in range(n0, n1 + 1):
        if (x + y) % 2 == 0:
            d.point((x, y), fill=INK)
# moon: solid black crescent (a gray can't exist on 1-bit)
d.ellipse([722, 22, 750, 50], fill=INK)
d.ellipse([730, 17, 758, 45], fill=BG)

# curve: pure-black 3-px segments (no antialiasing → no threshold gaps)
for a, b in zip(pts, pts[1:]):
    d.line([a, b], fill=INK, width=3)

# markers
d.ellipse([pts[0][0] - 5, pts[0][1] - 5, pts[0][0] + 5, pts[0][1] + 5], fill=BG, outline=INK, width=2)
d.ellipse([pts[9][0] - 4, pts[9][1] - 4, pts[9][0] + 4, pts[9][1] + 4], fill=INK)
d.ellipse([pts[12][0] - 4, pts[12][1] - 4, pts[12][0] + 4, pts[12][1] + 4], fill=INK)

# curve labels: 13.4° sits above its data point (x=636,y~135) on a white plate,
# tied to the point by a short leader so it can't read as floating
d.rectangle([606, 92, 686, 116], fill=BG)
put_text(d, 612, 96, '13.4°', INK, sc=2)
d.line([(636, 116), (636, 131)], fill=INK, width=2)   # leader down to the point
d.rectangle([682, 148, 754, 168], fill=BG)
put_text(d, 688, 152, '12.1°', INK, sc=2)

# axis (labels centered under their hour points: 15→240, 18→372, 21→505, 03→770)
ax_y = 184
put_text(d, 228, ax_y, '15', LBL, sc=2)
put_text(d, 360, ax_y, '18', LBL, sc=2)
put_text(d, 493, ax_y, '21', LBL, sc=2)
put_text(d, 758, ax_y, '03', LBL, sc=2)

# ---- left: current conditions --------------------------------------------
# 4 icons, one per 3h block: partly cloudy, clear, clear, cloudy
import math

def icon_sun(cx, cy, r=11):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=INK)
    for a in (0, 45, 90, 135, 180, 225, 270, 315):
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        x1, y1 = cx + c * (r + 3), cy + s * (r + 3)
        x2, y2 = cx + c * (r + 8), cy + s * (r + 8)
        d.line([(x1, y1), (x2, y2)], fill=INK, width=2)

def icon_cloud(cx, cy):
    d.ellipse([cx - 16, cy - 8, cx + 2, cy + 8], fill=INK)
    d.ellipse([cx - 8, cy - 14, cx + 8, cy + 2], fill=INK)
    d.ellipse([cx - 2, cy - 8, cx + 16, cy + 8], fill=INK)

def icon_part(cx, cy):
    d.ellipse([cx - 12, cy - 12, cx + 4, cy + 4], outline=INK, width=2)
    for a in (0, 45, 90, 135, 180, 225, 270, 315):
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        d.line([(cx - 4 + c * 11, cy - 4 + s * 11), (cx - 4 + c * 16, cy - 4 + s * 16)], fill=INK, width=2)
    d.ellipse([cx - 8, cy + 2, cx + 8, cy + 16], fill=INK)
    d.ellipse([cx, cy - 4, cx + 16, cy + 10], fill=INK)

IX0, IGAP, ICY = 20, 52, 56
icon_part(IX0 + 18, ICY)
icon_sun(IX0 + IGAP + 18, ICY)
icon_sun(IX0 + 2 * IGAP + 18, ICY)
icon_cloud(IX0 + 3 * IGAP + 18, ICY)

# big temperature
put_text(d, 14, 108, '19.7°', INK, sc=4)
put_text(d, 18, 196, 'FEELS 18°', MUT, sc=2)

# ---- bottom chips ---------------------------------------------------------
CH_Y, CH_H = 214, 40
d.rounded_rectangle([240, CH_Y, 414, CH_Y + CH_H], radius=10, fill=(23, 25, 30))
d.rounded_rectangle([426, CH_Y, 598, CH_Y + CH_H], radius=10, fill=(23, 25, 30))
d.rounded_rectangle([610, CH_Y, 782, CH_Y + CH_H], radius=10, fill=(23, 25, 30))
for x in (240, 426, 610):
    d.rounded_rectangle([x, CH_Y, x + 174, CH_Y + CH_H], radius=10, outline=INK, width=2)
cy2 = CH_Y + CH_H // 2
CTXT = (235, 238, 242)   # light ink for anything on the dark chips (INK would vanish on dark)

# chip 1: umbrella + DRY 12 H
d.pieslice([256, cy2 - 9, 278, cy2 + 9], 180, 0, fill=CTXT)
d.line([(267, cy2 + 1), (267, cy2 + 9)], fill=CTXT, width=2)
put_text(d, 288, cy2 - 7, 'DRY 12 H', CTXT, sc=2)

# chip 2: wind + 8 KM/H  (chip 2 spans 426-598)
for dy, ln in ((-4, 26), (2, 18)):
    d.line([(448, cy2 + dy), (448 + ln, cy2 + dy)], fill=CTXT, width=2)
    d.line([(448 + ln, cy2 + dy), (448 + ln - 4, cy2 + dy + 4)], fill=CTXT, width=2)
put_text(d, 486, cy2 - 7, '8 KM/H', CTXT, sc=2)

# chip 3: thermometer + 19.8 -> 12.1 (arrow drawn larger)
d.rectangle([632, cy2 - 10, 638, cy2 + 4], outline=CTXT, width=2)
d.ellipse([629, cy2 + 2, 641, cy2 + 14], fill=CTXT)
put_text(d, 652, cy2 - 7, '19.8', CTXT, sc=2)
ax = 652 + 4 * 12 + 2  # after "19.8"
d.line([(ax, cy2 - 8), (ax + 12, cy2 - 8)], fill=CTXT, width=2)
d.line([(ax, cy2 + 8), (ax + 12, cy2 + 8)], fill=CTXT, width=2)
d.polygon([(ax, cy2 - 10), (ax + 16, cy2), (ax, cy2 + 10)], fill=CTXT)
put_text(d, ax + 22, cy2 - 7, '12.1', CTXT, sc=2)

# ---- convert to 1bpp and emit header --------------------------------------
bw = img.convert('L')
px = bw.load()
rows = 272
rowbytes = W // 8
out = bytearray()
for y in range(rows):
    for x in range(rowbytes):
        b = 0
        for k in range(8):
            if px[x * 8 + k, y] < 128:
                b |= 0x80 >> k
        out.append(b)

hdr = ("// Desk-buddy 12h weather mock: 792x272 1bpp, row-major MSB-first\n"
       "// generated from the esp32s3_272x792 weather layout\n\n"
       "const uint8_t gImage_weather[272*792/8] = {\n")
lines = []
for i in range(0, len(out), 16):
    lines.append(','.join('0x%02x' % v for v in out[i:i + 16]) + ',')
body = '\n'.join(lines)
with open('fw/test_sketch/weatherimg.h', 'w', newline='\n') as f:
    f.write(hdr + body + '\n};\n')

img.save('fw/test_sketch/weather_preview.png')
print('wrote weatherimg.h (%d bytes) + preview png' % len(out))
