# 12h weather mock — defect log (2026-09-26)

**Status: all 12 defects FIXED & VERIFIED — flashed to panel, `panel up:` confirmed.**

Source of truth: `fw/test_sketch/weather_1bpp.png` (exact 1-bit panel output),
NOT `weather_preview.png` (color; light grays survive there but not on e-ink).
Method: pixel-count probes per region + zoomed vision review.

Status legend: [ ] open · [x] fixed & verified

## D1 — Axis labels invisible on device (MISSING TEXT)
`[x]` fixed & verified
Cause: axis color LBL=(167,173,182), luminance 172 > 128 → thresholds to white.
Evidence: pixel probe — rows 184–196 at x 228–770 = **0 ink** in 1bpp, but 28+
ink in color preview. Panel showed no hour axis at all.

## D2 — "FEELS 18°" line invisible (MISSING TEXT)
`[x]` fixed & verified
Cause: MUT=(125,131,139), luminance 130 > 128 → white.
Evidence: probe rows 196–210 = **0 ink** in 1bpp (452 in color).

## D3 — 13.4° curve label invisible (MISSING TEXT)
`[x]` fixed & verified
Cause: same LBL luminance issue (D1). Evidence: probe (566–626,100–114) = 0 ink.

## D4 — Moon invisible (MISSING TEXT)
`[x]` fixed & verified
Cause: MOON=(154,160,166), luminance 159 > 128 → white.
Fix: solid black crescent (black on white reads correctly on e-ink).

## D5 — Night band invisible (MISSING VISUAL)
`[x]` fixed & verified
Cause: NIGHT=(232,235,240) luminance 235 → pure white, indistinguishable.
Fix: 50 % checkerboard dither (light gray on 1-bit e-ink) for the night hours.

## D6 — Area fill under curve invisible (MISSING VISUAL)
`[x]` fixed & verified
Cause: LBLUE gradient luminance 205 → white. On 1-bit it could never show.
Fix: removed (curve line alone carries the trend).

## D7 — ° symbol renders as hollow square "tofu" on the big readout
`[x]` fixed & verified
Cause: 5×7 `°` glyph is a 3×3 box; at sc=4 its white center is 4 px wide,
reading as an empty box instead of a degree mark.
Fix: `°` redrawn as a compact 2×2 solid dot.

## D8 — Antialiased curve line risks gaps/broken segments
`[x]` fixed & verified
Cause: PIL antialiased 3-px line: edge pixels have intermediate luminance;
some fall > 128 → break the line after thresholding.
Fix: curve drawn as pure black 3-px segments; markers/labels solid black.

## D9 — Chip 3 ("19.8 → 12.1") cramped: arrow collides with digits
`[x]` fixed & verified
Cause: arrow drawn at fixed x with no spacing budget; digits + arrow + digits
tighter than the other chips' padding.
Fix: explicit spacing budget — icon | 19.8 | 24 px gap + arrow | 12.1.

## D10 — Axis labels not centered under their hour points (MISALIGNED)
`[x]` fixed & verified
Cause: labels placed at arbitrary x. Hour points: 15→240, 18→372, 21→505,
03→770; a 2-px-scale label is 24 px wide, so centering needs x = point−12.
Fix: labels repositioned to 228 / 360 / 493 / 758.

## D11 — Curve start marker floats 16 px below the icon row (MISALIGNED)
`[x]` fixed & verified
Cause: CY_TOP=44 while icon centers sit at y≈56.
Fix: CY_TOP=56 so the "now" point shares the icon baseline.

## D12 — 12.1° min label crowding/overlapping the curve near the right edge
`[x]` fixed & verified
Cause: label at y=122 sat on the descending line and the night band dithers
behind it → unreadable.
Fix: label moved to y=152, below the line, on a white plate (682,148)–(754,168).

## D13 — "8 KM/H" renders as "8 KM H" (MISSING GLYPH)
`[x]` fixed & verified
Cause: `/` absent from the 5×7 glyph set; `put_text()` silently skipped
unknown chars (it also silently skipped the space char — only the per-char
advance saved the spacing).
Fix: added `/` and ` ` glyphs; `put_text()` now raises on any unknown char,
so a missing glyph fails the build instead of vanishing from the display.

## D14 — Chip icons invisible: umbrella / wind / thermometer (INK on dark)
`[x]` fixed & verified
Cause: the three leading chip icons were drawn with `fill=INK` (black)
directly on the dark rounded-rect chip fill `(23,25,30)` — both threshold to
the same black on 1-bit, so the icons vanish. Only the light text
`(235,238,242)` was visible.
Evidence: pixel probe — 0 light-px in the umbrella (256–280), wind
(442–478) and thermometer (625–645) zones.
Fix: all three icons now drawn in the light chip-text color `(235,238,242)`.

## D15 — "13.4°" label disconnected from its data point
`[x]` fixed & verified
Cause: the 13.4 h data point is at x=636, but its label was placed at
x566–626 (~70px left of the point) with no leader, so it floats with nothing
tying it to the curve; the point's solid dot was also swallowed by the night
dither.
Fix: label moved to sit on the point (white plate 610,96–682,116; text at
616,100); the dither-swallowed dot dropped as redundant.

## D16 — "/" glyph renders as a broken/dotted diagonal
`[x]` fixed & verified
Cause: the 5×7 `/` glyph is 4 thin diagonal pixels (00001/00010/00100/01000);
at sc=2 on 1-bit it reads as a dashed line, not a solid stroke.
Fix: thickened the diagonal to 2-px-wide connected pixels so it reads solid.

---
## Verification method
After each fix batch: re-run `tools/render_weather.py`, regenerate `weather_1bpp.png`,
pixel-probe every formerly-broken region (must be > 0 ink where content exists,
0 where it must not), zoomed vision pass on crops, then reflash and confirm
serial `panel up: 12h weather mock shown`.

## Final verification (2026-09-26, round 2: D14–D16)
Pixel probes on `fw/test_sketch/weather_1bpp.png`:
- D14 chip icons (light px in icon zones, were 0): umbrella 200 · wind 110 ·
  thermometer 164 — all now visible on the dark chips.
- D15 13.4° plate text 188 ink px + leader line (636,116)–(636,131) tying the
  plate to its data point; vision confirms "clearly attached".
- D16 slash zone 296 light px; vision confirms solid diagonal in "8 KM/H".
Vision pass (2x/3x crops): umbrella + wind + thermometer icons present, text
clean, no overlaps.
Flashed: compile 345,691 B (26%) → upload COM4 → serial
`panel power: on` → `init: blank full refresh done` → `panel up: 12h weather mock shown` → deep sleep.
