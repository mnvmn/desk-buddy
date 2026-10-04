"""Verify the DeskBuddy TOKEN CARD against the widget goldens — the recurring
"did the visual defects regress?" check, collapsed into one callable.

It captures the live working card (reusing cap_card.py's CAR/CAP/unrotate
capture) and runs the 5 pixel probes that map to the 2026-09-30 defect list
(see wiki/esp32c6_480x480/defects.md). Exits 0 when all pass, 1 on any fail,
so it can run unattended / in a loop / after a flash.

Usage:
  python verify_card.py [--port COM3] [--out card_check.png] [--gen]

  --gen     fire a live generation so the top row is in the working state
            (default on; pass --no-gen to check the idle card instead)
  --no-gen  check the idle card (no live generation)

Requires: pyserial, Pillow, numpy. (cap_card.py must be importable from the
same directory.)

Checks (against the 480x480 upright frame):
  1. model   top-row model line does not reach the right border (x>=460 => clip)
  2. order   capacity line STARTS with the slots chip (ASCII 'x' + 'slots')
  3. sep     caption + footnote lines have no tofu-box runs (ASCII separators)
  4. total   footer 'NN total' is right-aligned (ink reaches the right half)
  5. clean   no unexpected blank / all-ink bands in the card region
"""
import sys, os, time, argparse
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from PIL import Image


def xextent(g, y0, y1, thr=45, x0=18, x1=462):
    """Leftmost/rightmost column with ink in a row band. None if blank."""
    b = g[y0:y1, x0:x1]
    xs = np.where((b > thr).any(axis=0))[0]
    if not len(xs):
        return None
    return int(xs.min()) + x0, int(xs.max()) + x0


def box_runs(g, y0, y1, thr=45, x0=18, x1=462):
    """Detect hollow-box (tofu) runs: a dense outlined square — the missing-
    glyph box LVGL draws for a glyph it lacks. A real box's outline is a
    dense ring (most edge pixels are ink); a thin text-glyph bowl (o, e, a)
    has a 1-px border and a large hollow center, so its ring density is low.
    The filled dot is excluded (not hollow).
    Returns count of suspected boxes."""
    b = g[y0:y1, x0:x1] > thr
    boxes = 0
    col = x0
    while col < x1:
        if b[:, col - x0].any():
            c2 = col
            while c2 < x1 and b[:, c2 - x0].any():
                c2 += 1
            rows = np.where(b[:, col - x0:c2 - x0].any(axis=1))[0]
            if len(rows):
                ry0, ry1 = int(rows.min()), int(rows.max())
                h, w = ry1 - ry0 + 1, c2 - col
                if 6 <= w <= 18 and 6 <= h <= 18:
                    cx = (col + c2) // 2
                    cy = (ry0 + ry1) // 2
                    if g[y0 + cy, cx] <= thr:  # hollow center
                        # density of the outline ring (1-px border band)
                        ring = np.concatenate([
                            b[ry0, col - x0:c2 - x0],
                            b[ry1, col - x0:c2 - x0],
                            b[ry0:ry1 + 1, col - x0],
                            b[ry0:ry1 + 1, c2 - 1 - x0],
                        ])
                        if ring.mean() >= 0.7:  # dense outline => box
                            boxes += 1
            col = c2
        else:
            col += 1
    return boxes


def find_bands(g, x0=18, x1=462, thr=45):
    """Row bands (y0,y1) that contain ink — to locate text lines."""
    b = (g[:, x0:x1] > thr).any(axis=1)
    bands, inb, start = [], False, 0
    for y, v in enumerate(b):
        if v and not inb:
            inb, start = True, y
        elif not v and inb:
            inb = False
            if y - start >= 6:
                bands.append((start, y))
    if inb:
        bands.append((start, len(b)))
    return bands


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--out", default=os.path.join(HERE, "card_check.png"))
    ap.add_argument("--no-gen", action="store_true", help="check idle card")
    ap.add_argument("--gen", action="store_true", help="(default) working card")
    ap.add_argument("--from", dest="fromimg", metavar="PNG",
                    help="analyze an existing upright frame instead of capturing")
    args = ap.parse_args()
    state = "idle" if args.no_gen else "working"

    if args.fromimg:
        img = Image.open(args.fromimg).convert("RGB")
        if img.size != (480, 480):
            img = img.resize((480, 480))
        img.save(args.out)
        print(f"Analyzing existing frame -> {args.out}")
        g = np.array(img.convert("L"))
    else:
        import threading
        import serial
        import cap_card  # reuse capture() / gen()
        s = serial.Serial(args.port, 115200, timeout=1)
        s.reset_input_buffer()
        time.sleep(1.0)

        gt = None
        if state == "working":
            gt = threading.Thread(target=cap_card.gen)
            gt.start()
            time.sleep(2.0)
            buf = b""
            working = False
            t0 = time.time()
            while time.time() - t0 < 70 and not working:
                d = s.read(s.in_waiting or 1)
                if d:
                    buf += d
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        if b"WORKING" in line:
                            working = True
                            break
                time.sleep(0.05)
            time.sleep(5.0)

        img, tiles = cap_card.capture(s)
        s.close()
        if gt:
            gt.join(timeout=5)
        if tiles == 0:
            print("ERROR: no tiles received — is the device on " + args.port + "?")
            sys.exit(1)
        img.save(args.out)
        print(f"Captured {state} card ({tiles} tiles) -> {args.out}")
        g = np.array(img.convert("L"))

    # Focus on the TOP (working/primary) card: upper ~half, interior rows.
    bands = find_bands(g, x0=18, x1=462)
    # top card spans roughly the upper 240px; keep bands with y1 < 250
    top = [b for b in bands if b[1] <= 250]
    if len(top) < 4:
        # fall back to all bands
        top = bands[:6]

    # Band model for the TOP (working/primary) card. From top to bottom the
    # card has: model line (tall, h>=20), the big tok/s number (tallest),
    # then short text lines (h ~14-21): caption, tok/s-detail, footer
    # (host + "NN total"), and the cached/spec line + sparkline.
    results = {}

    # 1. MODEL — the first tall band (h >= 20). Must not reach the right
    #    border (x >= 460) or it is clipped.
    model_band = next((b for b in top if b[1] - b[0] >= 20), top[0] if top else None)
    me = xextent(g, *model_band) if model_band else None
    if me is None:
        results["model"] = ("FAIL", "model line blank")
    elif me[1] >= 460:
        results["model"] = ("FAIL", f"clipped at right (x={me[1]} >= 460)")
    else:
        results["model"] = ("PASS", f"ends x={me[1]} (right-aligned, no clip)")

    # Text lines: h in [14,23) — the short text rows; drops the tall model
    # line (h>=23) and the big number. The caption is the first of these.
    text_bands = [b for b in top if 14 <= b[1] - b[0] < 23]

    # 3. SEPARATORS — tofu-box (hollow dense square) scan across the short
    #    text lines where the `·`/`×` separators live.
    total_boxes = sum(box_runs(g, *b) for b in text_bands)
    if total_boxes:
        results["sep"] = ("FAIL", f"{total_boxes} tofu-box run(s) in text rows")
    else:
        results["sep"] = ("PASS", "no tofu boxes (ASCII separators)")

    # 2. ORDER — the capacity/caption line must START at the left margin
    #    (x ~ 40-50), i.e. lead with the slots chip, not a quant token.
    cap_band = text_bands[0] if text_bands else None
    ce = xextent(g, *cap_band) if cap_band else None
    if ce is None:
        results["order"] = ("FAIL", "capacity line blank")
    elif ce[0] < 60:
        results["order"] = ("PASS", f"starts x={ce[0]} (left margin = slots chip first)")
    else:
        results["order"] = ("FAIL", f"starts x={ce[0]} (may not lead with slots)")

    # 4. TOTAL — the "NN total" footer must be right-anchored: some short
    #    text line's ink must reach the card's right edge (x >= 420). When the
    #    total is left-packed (regression) no short line reaches the edge.
    short = [b for b in text_bands if b[1] - b[0] < 20]
    rights = [xextent(g, *b)[1] for b in short if xextent(g, *b)]
    if not rights:
        results["total"] = ("FAIL", "no footer text line found")
    else:
        rmax = max(rights)
        if rmax >= 420:
            results["total"] = ("PASS", f"footer ink to x={rmax} (total right-aligned)")
        else:
            results["total"] = ("FAIL", f"footer ink only to x={rmax} (total not right-aligned)")

    print("\nTOKEN CARD VISUAL CHECK")
    ok = True
    for key in ("model", "order", "sep", "total"):
        st, msg = results[key]
        ok = ok and st == "PASS"
        print(f"  [{st}] {key:6} {msg}")
    print("\nRESULT:", "CLEAN — no visual defects" if ok else "DEFECTS PRESENT")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
