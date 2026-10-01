#!/usr/bin/env python
"""Render the net-status page as a 792x272 1bpp e-paper bitmap (MSB-first).

Output: fw/test_sketch/statusimg.h  (const uint8_t gImage_status[])
Format matches the EDP lib: 1bpp, row-major, MSB-first, 0x80=ink (black),
row width a multiple of 8 (792 ok).

Layout (landscape 792x272):
  left  : NET STATUS title, "API OPEN-METEO.COM" (the weather endpoint),
          a big wifi glyph (filled = linked), an SSID row, and a dark plate
  right : a 1-bit dither "ping" bar strip: 4 segments, all solid = a full
          link (the mock always shows a healthy link)
"""
import os, re
from PIL import Image, ImageDraw

W, H = 792, 272
BG  = (255, 255, 255)
INK = (24, 26, 30)   # main text / ink (must stay < 128 lum for 1-bit)
MUT = (60, 62, 66)   # secondary text
PLATE = (23, 25, 30) # dark plates (status pill, bar track)
LTXT  = (235, 238, 242)  # light text on dark plates

# ---- 5x7 pixel font (subset shared with render_weather.py) ----------------
GLYPHS = {
    '0': ['01110','10001','10011','10101','11001','10001','01110'],
    '1': ['00100','01100','00100','00100','00100','00100','01110'],
    '2': ['01110','10001','00001','00110','01000','10000','11111'],
    '3': ['11110','00001','00001','01110','00001','00001','11110'],
    '4': ['00010','00110','01010','10010','11111','00010','00010'],
    '5': ['11111','10000','11110','00001','00001','10001','01110'],
    '6': ['00110','01000','10000','11110','10001','10001','01110'],
    '7': ['11111','00001','00010','00100','00000','01000','01000'],
    '8': ['01110','10001','10001','01110','10001','10001','01110'],
    '9': ['01110','10001','10001','01111','00001','00110','01001'],
    'A': ['01110','10001','10001','11111','10001','10001','10001'],
    'B': ['11110','10001','10001','11110','10001','10001','11110'],
    'C': ['01110','10001','10000','10000','10000','10001','01110'],
    'D': ['11110','10001','10001','10001','10001','10001','11110'],
    'E': ['11111','10000','10000','11110','10000','10000','11111'],
    'F': ['11111','10000','11110','10000','10000','10000','10000'],
    'G': ['01110','10001','10000','10110','10001','10001','01111'],
    'H': ['10001','10001','10001','11111','10001','10001','10001'],
    'I': ['11111','00100','00100','00100','00100','00100','11111'],
    'K': ['10001','10010','10100','11000','10100','10010','10001'],
    'L': ['10000','10000','10000','10000','10000','10000','11111'],
    'M': ['10001','11011','10101','10101','10001','10001','10001'],
    'N': ['10001','11001','10101','10011','10001','10001','10001'],
    'O': ['01110','10001','10001','10001','10001','10001','01110'],
    'P': ['11110','10001','10001','11110','10000','10000','10000'],
    'R': ['11110','10001','10001','11110','10100','10010','10001'],
    'S': ['01111','10000','10000','01110','00001','00001','11110'],
    'T': ['11111','00100','00100','00100','00100','00100','00100'],
    'U': ['10001','10001','10001','10001','10001','10001','01110'],
    'W': ['10001','10001','10001','10101','10101','11011','10001'],
    'V': ['10001','10001','10001','10001','10001','01010','00100'],
    'l': ['11000','01000','01000','01000','01000','01000','11100'],
    '.': ['00000','00000','00000','00000','00000','00110','00110'],
    ':': ['00000','00110','00000','00000','00000','00110','00000'],
    '-': ['00000','00000','00000','11111','00000','00000','00000'],
    ' ': ['00000','00000','00000','00000','00000','00000','00000'],
}

def put_text(d, x, y, s, color, sc=2):
    # FAIL LOUDLY on unknown glyphs (silent skips have burned us before)
    missing = {c for c in s if c not in GLYPHS}
    if missing:
        raise SystemExit(f"put_text: glyphs missing from GLYPHS: {missing!r} in {s!r}")
    cx = x
    for ch in s:
        for r, row in enumerate(GLYPHS[ch]):
            for c, bit in enumerate(row):
                if bit == '1':
                    d.rectangle([cx + c*sc, y + r*sc, cx + c*sc + sc - 1,
                                 y + r*sc + sc - 1], fill=color)
        cx += 6 * sc
    return cx

def text_w(s, sc=2):
    return len(s) * 6 * sc

# ---- config from shared/net.env -------------------------------------------
def load_env():
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))  # .../esp32s3_272x792/tools -> repo root
    env = {}
    for line in open(os.path.join(root, 'shared', 'net.env'), encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if '=' in line:
            k, v = line.split('=', 1)
            env[k.strip()] = v.strip()
    return env

CFG = load_env()
DEV = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # esp32s3_272x792/
SSID = CFG['WIFI_SSID']
# This panel's only remote dependency is the weather API (no LAN endpoint)
API = CFG['WEATHER_HOST'].upper()

# ---- canvas ---------------------------------------------------------------
img = Image.new('RGB', (W, H), BG)
d = ImageDraw.Draw(img)

# left column
put_text(d, 24, 34, 'NET STATUS', INK, sc=4)
put_text(d, 26, 96, f'API {API}', MUT, sc=2)

# wifi glyph: filled = linked (three arcs + dot), drawn solid
def wifi_arc(cx, cy, r):
    d.arc([cx - r, cy - r, cx + r, cy + r], 225, 45, fill=INK, width=4)
CX, CY = 120, 178
wifi_arc(CX, CY, 14)
wifi_arc(CX, CY, 26)
d.ellipse([CX - 5, CY - 5, CX + 5, CY + 5], fill=INK)
put_text(d, 152, 168, 'LINK', MUT, sc=2)

# SSID plate (dark pill, light text)
ip = SSID
pill_w = text_w(ip, 2) + 28
d.rounded_rectangle([24, 216, 24 + pill_w, 258], radius=10, fill=PLATE)
put_text(d, 38, 230, ip, LTXT, sc=2)

# right: ping bar strip - 4 segments in a dark track (all lit = healthy)
TR_X0, TR_X1, TR_Y0, TR_Y1 = 360, 768, 84, 188
d.rounded_rectangle([TR_X0, TR_Y0, TR_X1, TR_Y1], radius=12, fill=PLATE)
seg = (TR_X1 - TR_X0 - 24) // 4
for i in range(4):
    x0 = TR_X0 + 12 + i * seg + (i * 8)
    # checkerboard dither inside = "signal" texture on 1-bit e-ink
    for y in range(TR_Y0 + 10, TR_Y1 - 9):
        for x in range(x0, x0 + seg - 8):
            if (x + y) % 3 != 0:
                d.point((x, y), fill=(200, 202, 206))  # light fill on dark track
put_text(d, 330, 210, f'WIFI {SSID}   ASSOCIATED', MUT, sc=2)

# ---- convert to 1bpp and emit header --------------------------------------
bw = img.convert('L')
px = bw.load()
rowbytes = W // 8
out = bytearray()
for y in range(H):
    for x in range(rowbytes):
        b = 0
        for k in range(8):
            if px[x * 8 + k, y] < 128:
                b |= 0x80 >> k
        out.append(b)

hdr = ("// Desk-buddy net status page: 792x272 1bpp, row-major MSB-first\n"
       "// generated by esp32s3_272x792/tools/render_status_page.py\n\n"
       "const uint8_t gImage_status[272*792/8] = {\n")
lines = [','.join('0x%02x' % v for v in out[i:i + 16]) + ','
         for i in range(0, len(out), 16)]
with open(os.path.join(DEV, 'fw', 'test_sketch', 'statusimg.h'), 'w', newline='\n') as f:
    f.write(hdr + '\n'.join(lines) + '\n};\n')
img.save(os.path.join(DEV, 'fw', 'test_sketch', 'status_preview.png'))
print(f'wrote statusimg.h ({len(out)} bytes) + status_preview.png')
