#!/usr/bin/env python
"""Host simulation of the live weather UI — same coordinates as
fw/weather_test/weather_test.ino. Draw sample data with the same 5x7 glyph
font and primitives, so the device capture can be compared 1:1."""
from PIL import Image, ImageDraw
import math

W, H = 800, 272
BG    = (255, 255, 255)
INK   = (24, 26, 30)
MUT   = (60, 62, 66)
LBL   = (45, 48, 54)
PLATE = (23, 25, 30)
LTXT  = (235, 238, 242)

GLYPHS = {
    '0': ['01110','10001','10011','10101','11001','10001','01110'],
    '1': ['00100','01100','00100','00100','00100','00100','01110'],
    '2': ['01110','10001','00001','00110','10100','10000','11111'],
    '3': ['11110','00001','00001','01110','00001','00001','11110'],
    '4': ['00010','00110','01010','10010','11111','00010','00010'],
    '5': ['11111','10000','11110','00001','00001','10001','01110'],
    '6': ['00110','10000','10000','11110','10001','10001','01110'],
    '7': ['11111','00001','00010','00100','01000','01000','01000'],
    '8': ['01110','10001','10001','01110','10001','10001','01110'],
    '9': ['01110','10001','10001','01111','00001','00110','10101'],
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
    'A': ['01110','10001','10001','11111','10001','10001','10001'],
    'G': ['01110','10001','10000','10110','10001','10001','01111'],
    'I': ['11111','00100','00100','00100','00100','00100','11111'],
    'O': ['01110','10001','10001','10001','10001','10001','01110'],
    'N': ['10001','11001','10101','10011','10001','10001','10001'],
    'P': ['11110','10001','10001','11110','10000','10000','10000'],
    'W': ['10001','10001','10001','10101','10101','11011','10001'],
    'T': ['11111','00100','00100','00100','00100','00100','00100'],
    'V': ['10001','10001','10001','10001','10001','01010','00100'],
    'U': ['10001','10001','10001','10001','10001','10001','01110'],
    'B': ['11110','10001','10001','11110','10001','10001','11110'],
    'C': ['01110','10001','10000','10000','10000','10001','01110'],
    'J': ['01111','00001','00001','00001','10001','01001','00110'],
    '-': ['00000','00000','00000','11111','00000','00000','00000'],
    '.': ['00000','00000','00000','00000','00000','00110','00110'],
    ':': ['00000','00110','00000','00000','00000','00110','00000'],
    '/': ['00000','00011','00110','01100','11000','00000','00000'],
    ' ': ['00000','00000','00000','00000','00000','00000','00000'],
    '\u00b0': ['00000','01100','01100','00000','00000','00000','00000'],
}

def put_text(d, x, y, s, color, sc=2):
    missing = {c for c in s if c not in GLYPHS}
    assert not missing, f"missing glyphs {missing} in {s!r}"
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

# ---- sample data (same shapes as real values) -----------------------------
CUR, FEELS, WIND, CODE = 14.9, 13.6, 5.3, 0
TEMPS = [14.9, 14.8, 14.8, 14.1, 12.7, 11.3, 10.3, 9.7, 8.8, 8.4, 8.0, 7.6, 7.1]
CODES = [0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0]  # 13 hourly WMO codes
RAIN = False
LO, HI = 7.1, 14.9
TIME = '21:30'
SUNRISE = '07:12'
SUNSET = '19:45'

def is_daytime(h):
    """Mirror of weather_test.ino: slot h (hours into now) is day if inside
    [sunrise, sunset], wrapping midnight. h is the forecast-slot offset."""
    cur = int(TIME[:2]) * 60 + int(TIME[3:])
    t = (cur + h * 60) % 1440
    sr = int(SUNRISE[:2]) * 60 + int(SUNRISE[3:])
    ss = int(SUNSET[:2]) * 60 + int(SUNSET[3:])
    if sr < ss:
        return sr <= t < ss
    return t >= sr or t < ss  # night straddles midnight

def fmt(v, nd=1):
    if v < 0:
        s = '-' + ('%.*f' % (nd, -v))
    else:
        s = ('%.*f' % (nd, v))
    return s

img = Image.new('RGB', (W, H), BG)
d = ImageDraw.Draw(img)

# ---- shared layout constants (mirror weather_test.ino) ---------------------
CX0, CX1, CY_T, CY_B = 240, 770, 56, 170
IX0, IGAP, ICY = 20, 52, 56
CH_Y, CH_H = 214, 40

# ---- condition icons -------------------------------------------------------
def circle(d, cx, cy, r, fill=None, outline=None, ow=2):
    if fill:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
    if outline:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=outline, width=ow)

def icon_sun(d, cx, cy, r=11):
    circle(d, cx, cy, r, outline=INK, ow=2)
    for a in range(0, 360, 45):
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        d.line([(cx + c*(r+3), cy + s*(r+3)), (cx + c*(r+8), cy + s*(r+8))],
               fill=INK, width=2)

def icon_moon(d, cx, cy, r=11):
    # full ink disc knocked out by an offset paper disc (crescent opens upper-left)
    circle(d, cx, cy, r, fill=INK)
    circle(d, cx + r // 2, cy - r // 3, r, fill=BG)

def icon_cloud(d, cx, cy):
    d.ellipse([cx - 16, cy - 8, cx + 2, cy + 8], fill=INK)
    d.ellipse([cx - 8, cy - 14, cx + 8, cy + 2], fill=INK)
    d.ellipse([cx - 2, cy - 8, cx + 16, cy + 8], fill=INK)

def icon_part(d, cx, cy):
    d.ellipse([cx - 12, cy - 12, cx + 4, cy + 4], outline=INK, width=2)
    for a in range(0, 360, 45):
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        d.line([(cx - 4 + c*11, cy - 4 + s*11), (cx - 4 + c*16, cy - 4 + s*16)],
               fill=INK, width=2)
    icon_cloud(d, cx, cy + 8)

for i in range(4):
    c = CODE if i == 0 else CODES[i * 3]
    x = IX0 + i * IGAP + 18
    if c == 0:
        if is_daytime(i * 3):
            icon_sun(d, x, ICY)
        else:
            icon_moon(d, x, ICY)
    elif c in (1, 2):
        icon_part(d, x, ICY)
    else:
        icon_cloud(d, x, ICY)

# ---- left: big temp + feels ------------------------------------------------
put_text(d, 14, 108, fmt(CUR) + '\u00b0', INK, sc=4)
put_text(d, 18, 196, 'FEELS ' + fmt(FEELS) + '\u00b0', MUT, sc=2)

# ---- right: temp curve ------------------------------------------------------
lo, hi = min(TEMPS) - 1.0, max(TEMPS) + 1.0
pts = []
for i, t in enumerate(TEMPS):
    px = CX0 + i * (CX1 - CX0) // 12
    py = CY_T + int((hi - t) / (hi - lo) * (CY_B - CY_T))
    pts.append((px, py))

# night band (sunset 20:23 -> from hour 5 onward)
sh = int(SUNSET[:2]) + (1 if int(SUNSET[3:]) >= 30 else 0)
i0 = sh - int(TIME[:2])
if i0 < 1: i0 = 1
n0, n1 = pts[i0][0] - 3, pts[12][0] + 3
for y in range(CY_T - 14, CY_B + 1):
    for x in range(n0, n1 + 1):
        if (x + y) % 2 == 0:
            d.point((x, y), fill=INK)

# moon
circle(d, 736, 22, 14, fill=INK)
circle(d, 743, 17, 14, fill=BG)

# curve: pure-black 3px
for a, b in zip(pts, pts[1:]):
    d.line([a, b], fill=INK, width=3)
circle(d, *pts[0], 5, fill=BG, outline=INK, ow=2)
circle(d, *pts[9], 4, fill=INK)
circle(d, *pts[12], 4, fill=INK)

# data labels: current (above its point at hour 9, mockup style) + range (upper left)
d.rectangle([606, 62, 686, 86], fill=BG)
put_text(d, 612, 66, fmt(TEMPS[9]) + '\u00b0', INK, sc=2)
d.line([(636, 86), (636, pts[9][1] - 5)], fill=INK, width=2)
put_text(d, 246, 12, 'RANGE ' + fmt(LO) + ' / ' + fmt(HI) + '\u00b0', MUT, sc=2)

# hour axis (12h window: 12 -> 00; hour+12 mod 24)
def hour_label(k):
    h = (int(TIME[:2]) + k) % 24
    return ('%02d' % h)
for i, lab in ((0, hour_label(0)), (3, hour_label(3)),
               (6, hour_label(6)), (12, hour_label(12))):
    x = pts[i][0] - 12
    put_text(d, x, 184, lab, LBL, sc=2)

# ---- bottom chips -----------------------------------------------------------
for x in (240, 426, 610):
    d.rounded_rectangle([x, CH_Y, x + 174, CH_Y + CH_H], radius=10, fill=PLATE)
cy2 = CH_Y + CH_H // 2

# chip 1: umbrella + DRY/RAIN 13H
d.pieslice([256, cy2 - 9, 278, cy2 + 9], 180, 0, fill=LTXT)
d.line([(267, cy2 + 1), (267, cy2 + 9)], fill=LTXT, width=2)
put_text(d, 290, cy2 - 7, ('DRY ' if not RAIN else 'RAIN ') + '13H', LTXT, sc=2)

# chip 2: wind lines + N KM/H
for dy, ln in ((-4, 26), (2, 18)):
    d.line([(448, cy2 + dy), (448 + ln, cy2 + dy)], fill=LTXT, width=2)
    d.line([(448 + ln, cy2 + dy), (448 + ln - 4, cy2 + dy + 4)], fill=LTXT, width=2)
put_text(d, 490, cy2 - 7, fmt(WIND) + ' KM/H', LTXT, sc=2)

# chip 3: thermometer + LO -> HI
d.rectangle([630, cy2 - 10, 636, cy2 + 4], outline=LTXT, width=2)
circle(d, 633, cy2 + 8, 6, fill=LTXT)
put_text(d, 648, cy2 - 7, fmt(LO), LTXT, sc=2)
ax = 648 + 4 * 12 + 2
d.line([(ax, cy2 - 8), (ax + 12, cy2 - 8)], fill=LTXT, width=2)
d.line([(ax, cy2 + 8), (ax + 12, cy2 + 8)], fill=LTXT, width=2)
d.polygon([(ax, cy2 - 10), (ax + 16, cy2), (ax, cy2 + 10)], fill=LTXT)
put_text(d, ax + 22, cy2 - 7, fmt(HI), LTXT, sc=2)

# top-right timestamp (time sits left of the moon, on the same row)
put_text(d, 686, 16, TIME, MUT, sc=2)

out = 'cache_weather_sim.png'
img.save(out)
print('wrote', out)
