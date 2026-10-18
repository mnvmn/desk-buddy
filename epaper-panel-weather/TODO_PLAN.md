# TODO plan — epaper-panel-weather

**Updated:** 2026-10-18 · Open work for the CrowPanel 5.79" e-paper project.
The web mockup `web/panel_v2/` is the source of truth. The v2.1 delta detail
in item 1 was merged in from `V2_UI_PORT_PLAN.md` (deleted 2026-10-17; its
completed F1–F19 port history remains in git history).

## 1. v2.1 firmware re-port — PORTED + HOST-VERIFIED (flash + golden CAP remain)

The logic port is complete and **host-verified pixel-identical on the v2.1
icon rows**: `tools/host_render.cpp` compiles the REAL `panel_fetch.cpp` +
`panel_ui.cpp` against a stubbed 800×272 1bpp canvas, feeds the web's exact
`mockWx()` data (now=12:30, `state=CLEAR`), and diffs against a live capture
of `web/panel_v2.html?mode=mock` → **left icon row = 0 px, 2-day strip =
0 px** (residual px sit in the pre-existing v2.0 curve — see note below).
**Layout swap 2026-10-18** (web + firmware): the three info rows moved to the
**top** of the left column (temp range / wind / rain-state) and the 4-icon row
to the **bottom** (single row); re-verified the same way — icon row 0 px,
info rows 0 px, strip 0 px (4 residual px — all on the rain-state drop
glyph; the earlier 422-px curve/label residual was closed by the 2026-10-18
spacing pass, which added the missing curve hour-label clamp into
`[CX0, CX1]`). The swap also exposed + fixed a pre-existing firmware
typo: the wind row printed `ms` instead of `m/s` (missing `/` glyph).
The spacing pass set the left-column vertical rhythm to ink gaps
17/17/15/18/17/21 (top 32 / bottom 21): info rows y 32/62/92, big temp
y 120, `DOW DD` y 165, city y 195, icon row `ICY=239` — identical in
`ui.js` and `panel_ui.cpp`/`panel_canvas.h`.
The ESP32 firmware **compiles clean** (`tools/flash_test.sh` FQBN, 78% flash /
26% RAM). `web/shot_v2_mock.png` was **regenerated 2026-10-18** from the live
v2.1 web mock (mock mode, new layout: info rows top, icon row bottom).

The firmware (`fw/weather_test/` — control flow in `weather_test.ino`, UI in
`panel_ui.cpp`, data in `panel_data.h`) is now at parity with the web on all
v2.1 icon logic. The original pre-v2.1 behaviour (8 small per-3h strip icons,
left icon row as point samples at now/+3/+6/+9h with a sun/moon split) has
been replaced:

- [x] **Fetch** — GET-1 `forecast_hours=13` → **`24`**; add `codes24[24]`
  (hourly WMO codes from now) to the `WX` struct. Curve / rain-state / rows
  keep using the first 13h as today. (web: `data.js` — reference render
  re-captured as `web/shot_v2_mock.png`.)
- [x] **Icons** — ported the medium set from `icons.js` 1:1 as
  `m_cloud/m_sun/m_part/m_rain/m_snow/m_bolt` (~19–22px; web names
  `mCloud/mSun/...`), plus the `strip_icon(code)` selector (web: `smallIcon`
  renamed `stripIcon`) with the **exact WMO code lists**:
  bolt `95/96/99`; snow `71/73/75/77/85/86`; sun `0` (day *and* night — no
  moon split); part `1/2`; rain `≥51`; else cloud.
  Gotcha: `m_cloud`'s flat base starts at `cy+1` (not `cy+2`) — closes a
  3px×1px white slit between the side circles and the base.
- [x] **`block_code(codes, start, n)`** — dominant code of an n-hour window:
  any storm > any snow > any rain > most-frequent dry code, dry ties toward
  the more overcast (`3/45/48 > 1/2 > 0`). Rain list: `51–67/80–82`.
  **Not a point sample** — all 24 hours feed the 4 icons (replaces
  `codes[k*6]` sampling; originally 8 icons at a 3h step).
- [x] **`left_part(k, R)`** — index range `[a,b)` of part k of 4 **equal parts
  from now to midnight**, R = 24−now_hour whole hours; min 1 hour per part
  (overlap allowed) when R<4.
- [x] **Left icon row** — 4 icons via `block_code`+`strip_icon` over
  `codes24` (replaces the point samples; rain/snow/storm now visible there).
- [x] **2-day strip** — 4 per-6h `strip_icon` icons per day (via
  `block_code`), pitch **52px**, right-aligned block (replaces 8 per-3h).
- [x] **Build + flash + verify** — build ✅ clean (2026-10-17). Flash via
  `tools/flash_test.sh` (COM4) ✅ **flashed 2026-10-18** + on-device capture
  via `tools/capture_weather_live.py` ✅. Live boot log: `panel power: on`,
  wifi up, `wx: TLS verified`, **`forecast_hours=24` fetch working on hardware**,
  `ui: live weather drawn`, `panel up: BRATISLAVA live shown` — no
  `font: missing glyph`. On-device render confirmed (via `CAP`): left column
  shows the **4 block icons**, the 2-day strip shows **MON + TUE each with 4
  medium per-6h icons**, and the info rows read temp-range / wind / DRY (top
  to bottom) — the v2.1 layout is correct on hardware. **Re-flashed
 2026-10-18 after the layout swap**: on-device capture confirmed the new
 layout live (info rows top, 4-icon single row bottom) and the golden was
 re-captured to `tools/device_v22_capture.png` (+ `..._2x.png`; the older
 `device_v21_capture*.png` pre-dates the swap — buffer is rotated 180° vs the
 web mock). **Re-flashed a 3rd time 2026-10-18 after the spacing/rhythm
 pass** (also carrying the curve hour-label clamp fix): on-device capture
 `tools/device_v23_capture.png` (+ `..._2x.png`) — the device's left column
 is pixel-identical to the web mock (same band y-extents: 32/62/92/120/165/
 195/222..251).
  **Also a latent parse bug was fixed on the way in**:
  `parse_num_array` was writing float bit-patterns into the int `codes` array
  (3.0f → 0x40400000) so every icon read as cloud; it now parses to a float
  temp buffer and casts to int (both `fetch_weather` and `fetch_two_days`).
- [x] **Docs** — flip the ⚠️ note in `wiki/epaper-panel-weather/README.md`
  to done (with date). ✅ flipped 2026-10-17.

**Pre-existing deltas the port deliberately does NOT touch** (out of v2.1 scope;
host render vs web mock shows **4 px** after the 2026-10-18 swap + spacing
pass, all in the rain-state row — not the icon rows, not the curve):

- **Rain-state drop glyph** — ~4 px of sub-pixel dither on the rain-row drop
  glyph (web `rowRain` vs firmware differ by a pixel or two). Cosmetic;
  present before this port. (The earlier ~418 px of curve dither/hour-label
  edges was NOT a deliberate delta — the firmware was missing the web's
  hour-label clamp into `[CX0, CX1]`; added in the 2026-10-18 spacing pass,
  curve is now 0 px.)
- **No F12 rain-state diff (verified 2026-10-17):** both web `rainState()`
  and firmware `rain_state()` return `"DRY"` for dry conditions — they
  already agree (a harness `state_ov="CLEAR"` pin had created a false
  "CLEAR vs DRY" note, since removed).
- **Display orientation is 180° (pre-existing, `Rotation=180` in EPD.h).** The
  EDP `Paint_SetPixel` applies a full 180° flip (`X=WM-x-1, Y=HM-y-1`) to the
  framebuffer, so a `CAP` capture of the live device is upside-down relative
  to the web mock / `tools/host_render` (whose stub omits the flip). The panel
  is physically mounted to compensate, so it reads correctly in hand. Not a
  port regression — same setting since v2.0 bring-up (see bringup.md).

**Constraints** (carried over from the old port plan, still binding):

- No RTC — every date-derived value comes from the forecast's date fields,
  never the wall clock.
- WMO code sets are shared verbatim with the web so web and firmware classify
  identically.
- Wind is km/h from the API → display m/s (`/3.6`).
- 1-bit e-ink: strip icons stay ≥ ~19–22px (the ~9px set was unreadable —
  `defects.md` D14–D16).
- URL length: the combined request path is ~365 chars — keep the `http_get`
  req + per-fetch path buffers ≥ 512 (a `snprintf` truncation 400s silently).

## 2. Housekeeping

- [ ] Commit the cleanup (stale-file removals — incl. `V2_UI_PORT_PLAN.md`,
  deleted 2026-10-17 with its pending work folded into this file — +
  retargeted tools + doc updates + this file).

## 3. Backlog (optional / later)

- [x] **Monolith split** — DONE 2026-10-17: `weather_test.ino` (was ~1380
  lines) is now control flow only; modules `panel_canvas` / `panel_font` /
  `panel_draw` / `panel_data` / `panel_net` / `panel_fetch` / `panel_state` /
  `panel_ui` under `fw/weather_test/`. Code moved verbatim (verified 0 lines
  missing/added), compiles clean, and a live-data A/B capture (original vs
  refactored, `MOCK 15:0`) is **pixel-identical** (0/217,600 px diff).
- [ ] **`tools/sim_weather_ui.py`** — host simulator of the UI, currently
  pre-v2.1: either update it to the v2.1 layout (for 1:1 device comparison)
  or retire it in favor of browser screenshots of the web mock.
- [ ] **Enclosure** — `case/` holds the CrowPanel back-plate + bezel STLs;
  print/fit verification is a separate workstream (source links in
  `case/doc.md`).
