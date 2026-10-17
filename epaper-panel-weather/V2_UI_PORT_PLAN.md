# Firmware v2 UI port — web `panel_v2` → `fw/weather_test`

**Status:** ✅ **DONE — all 19 features (F1–F19)** ported to the monolith and verified on
hardware (COM4) · ⚠️ **web mockup has since changed (v2.1 strip): 4 medium icons per 6h
instead of 8 small per 3h — pending firmware re-port** (see delta below) ·
**Target:** `fw/weather_test/` · **Companion:** [`REFACTOR_PLAN.md`](REFACTOR_PLAN.md) (firmware split, still not started)

## ⚠️ Web v2.1 delta — pending firmware re-port (2026-10-17, after full port)

The web mockup `web/panel_v2/` changed the 2-day strip: the 8 small (~9px) per-3h icons
were replaced by **4 medium (~19–22px) icons per 6h block** (readability on the panel).
The firmware (`weather_test.ino`) still renders the old 8-per-3h strip — **re-port needed**:

- `icons.js`: new medium icons `mCloud/mSun/mPart/mRain/mSnow/mBolt` (1:1 ports of
  `m_cloud/m_sun/m_part/m_rain/m_snow/m_bolt`); `smallIcon` **renamed `stripIcon`**
  (same WMO code lists, no day/night moon split — code 0 → `mSun` always).
- `icons.js` `mCloud`: flat base starts at `cy+1` (not `cy+2`) — closes a 3px×1px white
  slit between the side circles and the base.
- `ui.js` `drawTwoDayStrip`: loop `k=0..3` with **6h step** (`codes[k*6]`), icon pitch
  **52px**, right-aligned block (was 8 icons, 3h step, 27px pitch).
- Reference render re-captured: `web/shot_v2_mock.png`.
- All other v2 features (F1–F19) are unchanged and already ported.

## Progress — ALL DONE (2026-10-17)

All 19 features ported into the monolith (`weather_test.ino`, option 2), compiled clean
(78% flash / 26% RAM), flashed to COM4, and verified against live captures + the `MOCK`/`STATE`
test commands (see "Verification" below).

- **Steps 1–3 (F1 F2 F3 F4 F5 F6 F18) — DONE, verified live.** `fetch_weather` does one
  request for current + hourly(13h, +`wind_speed_10m`) + 3-day daily, capturing the current
  date and today's sunrise/sunset. `fetch_two_days()` fills `wx.days[2]` (codes/temps/rain +
  daylight lo/hi). Boot log confirms: 3-day dates, `sunrise 7:13 sunset 17:59`,
  `day0 … lo=11.7 hi=19.7`, `day1 … lo=5.5 hi=16.1`.
  - *Impl notes:* `url1` is 365 chars → `http_get` `req` + `path` buffers bumped → **512**
    (a truncated URL 400s silently). The JSON string scanners step past the **closing** quote
    (`c = e + 1`).
- **Step 4 (F8 F9 F11) — DONE, verified.** small strip icons (`s_cloud/sun/moon/part/rain/
  snow/bolt`), `small_icon` selector (exact WMO code lists), row glyphs (`row_rain/wind/therm`).
  Verified: the 2-day strip shows varied small icons (cloud/sun/snowflake) legible at ~9px.
- **Step 5 (F12) — DONE, verified.** `rain_state()` 4-state classifier (storm>snow>rain>dry)
  + `state_ov` serial override. Verified STORM (bolt) + RAIN (umbrella + `0.0MM`) + DRY rows.
- **Step 6 (F10 F16) — DONE, verified.** `days_from_civil` + `moon_phase()` (synodic 29.53 d,
  ref 2000-01-06) + `draw_moon_phase()`; the glyph is drawn on the "till night" countdown.
  Verified: `MOCK 15:0` → `3h` sunset countdown + crescent moon glyph.
- **Step 7 (F13 F14) — DONE, verified.** left-column restructure (icons @52, big temp @96,
  `SAT 17` day label, city @156 sc1) + the three info rows (rain-state / wind m/s / temp °C);
  the three bottom badges are **removed**.
- **Step 8 (F15) — DONE, verified.** `draw_two_day_strip()`: `SUN`/`MON` + 8 per-3h small
  icons each + `DRY` + daylight `lo-hi°C`.
- **Step 9 (F17 F19) — DONE, verified.** parity: `CY_B` 170→166, "now" marker hollow-ring →
  filled dot, hour-axis/night-band coords. F19: serial `STATE DRY|RAIN|SNOW|STORM|RESET`
  override (STORM/RAIN/SNOW verified; `RESET` restores live).

### Verification (on hardware, COM4)

- Live capture (`MOCK` off) → left column (4 icons, `15.1`, `SAT 17`, `BRATISLAVA`, rows
  `11.7-15.1°C` / `0.8-4.7 m/s` / `DRY`), 13h curve with night band + hour labels + end plate,
  2-day strip `SUN`/`MON` with 8 icons + `DRY` + `11.7-19.7°C` / `5.5-16.1°C`.
- `MOCK 15:0` → `3h` sunset countdown + moon-phase glyph (F10/F16).
- `STATE STORM` / `STATE RAIN` / `STATE SNOW` → the matching glyph + label row (F12).
- `STATE RESET` + city toggle → back to live BRATISLAVA.

The v2 design is **done and verified in the web mockup** `web/panel_v2/`
(canvas / font / icons / ui / data / app). The
firmware `fw/weather_test/weather_test.ino` (monolith) started on the **v1.0
layout** (4 left icons + big temp + city, a 13h curve, and **three bottom
badges** DRY/RAIN · wind · temp range) and is now on the **v2 render**. This
plan lists the features that were ported to get there, and where each landed.

Reference render (target): `web/shot_v2_mock.png`.

## What already exists in the firmware (do NOT redo)

These were pre-work for v2 and are committed but **unused by the UI today**:

- `GLYPHS[]` 40–45 — `~`(40) `°`(41) `>`(42) `_`(43) `m`(44) `s`(45)
  (`weather_test.ino:113-118`, `glyph_idx:135-140`).
- Extended `WX` struct — `winds[13]`, `now_year/month/day`, `WXDay days[2]`
  (`weather_test.ino:298-313`).
- Large condition icons — `icon_sun/moon/cloud/part` (already match the web).

The gaps below are everything the web has that the firmware does not.

---

## Feature list

Grouped by layer, dependency-ordered (data → font → draw → UI → orchestration).
Each row: **web source** → **firmware target** (post-refactor module; see
`REFACTOR_PLAN.md` for the module map).

### A. Data / fetch — `panel_fetch.cpp`

| # | Feature | Web source | What to port |
|---|---------|-----------|--------------|
| F1 | **Hourly wind (m/s)** | `data.js` `fetchLive` (adds `wind_speed_10m` to the hourly request) | `fetch_weather` currently requests only `temperature_2m,weather_code,precipitation`. Add `wind_speed_10m`, parse into `wx.winds[13]`, convert **km/h → m/s** (`/3.6`). `winds[13]` is declared but never populated today. Needed by F14. |
| F2 | **3-day daily** | `data.js` `fetchLive` url1 (`daily=…&forecast_days=3`) | Extend the daily request from `forecast_days=1` to **3**, adding `weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,sunrise,sunset`. Gives the 2 future dates + per-day sunrise/sunset. Feeds F3 and `fetch_sunset` (today day-1 only). |
| F3 | **2-day hourly fetch** | `data.js` `fetchLive` url2 (`start_date`/`end_date`) | New `fetch_two_days()`: GET `hourly=temperature_2m,weather_code,precipitation&start_date=<day1>&end_date=<day2>`. Populate `wx.days[2]` (date, `codes[24]`, `temps[24]`, `rain` total, per-day `lo`/`hi` via F6). `days[2]` is declared but never filled today. |
| F4 | **Capture current date** | `data.js` `fetchLive` (uses `hourly.time[0]` date) | `fetch_weather` reads only `HH:MM` from `hourly.time[0]`. Also parse the `YYYY-MM-DD` prefix into `wx.now_year/now_month/now_day` (declared, never captured). Feeds F5/F10. |
| F5 | **Day-of-week from date** | `data.js` `dayLabel`/`dayDow` (via `new Date`) | **No RTC on the board** (web uses wall clock). Implement a calendar dow (e.g. Tomohiko Sakamoto) used for the current day (F13) and the two strip dates (F15). Inputs are the date fields from F4/F3, not a clock. |
| F6 | **Daylight lo/hi helper** | `data.js` `daylightRange` | min/max of hourly temps over daylight hours (sr/ss in hours; fallback 05–19h, then all hours, then 0). Two call shapes: the **13h current window** (left rows, F14) and the **24h per-day** (strip, F15). |

### B. Font — `panel_font.h`

| # | Feature | Note |
|---|---------|------|
| F7 | **Range/glyph chars** | **No new glyphs required** — `° m s -` (41/44/45/33) already present (above). Verify the chosen range separator renders; the web defaults to `-` (index 33) and only `~ > _` exist as alternates. Action = verify, not add. |

### C. Draw / icons — `panel_draw.h`

| # | Feature | Web source | What to port |
|---|---------|-----------|--------------|
| F8 | **Medium strip icons (v2.1)** | `icons.js` `mCloud/mSun/mPart/mRain/mSnow/mBolt` | ~19–22px procedural icons for the 2-day strip (v2.1, 2026-10-17; superseded the ~9px `s*` set — `sSnow`/`sBolt` remain for the left-column state rows). Firmware currently has the old small `s_*` strip icons — re-port as `m_*`. |
| F9 | **`stripIcon` selector (v2.1)** | `icons.js` `stripIcon` (was `smallIcon`) | WMO `code` → medium icon: bolt `95/96/99`; snow `71/73/75/77/85/86`; sun `0` (day *and* night — no moon split in v2.1); part `1/2`; rain `≥51`; else cloud. **Port the exact code lists** so classification matches the web. |
| F10 | **Moon phase** | `icons.js` `moonPhase`/`drawMoonPhase` | Compute phase (synodic 29.53058867 d; ref new-moon 2000-01-06 18:14 UTC) **from the current date** (F4), not wall clock; draw the terminator-shaded disc (lit = INK). |
| F11 | **Left-column row glyphs** | `icons.js` `rowRain`/`rowWind`/`rowTherm` | Small (~12px) procedural glyphs for the three info rows (umbrella, wind arrow, thermometer). Distinct from the big badge icons being removed. |

### D. UI composition — `panel_ui.cpp`

| # | Feature | Web source | What to port |
|---|---------|-----------|--------------|
| F12 | **Rain-state classification** | `ui.js` `rainState` | 4-state `DRY / RAIN / SNOW / STORM` from the 13h WMO codes + precip, severity priority: storm `95/96/99` > snow `71/73/75/77/85/86` > rain `rain_total ≥ 0.5` > dry. Firmware today only does a DRY/RAIN binary in badge 1. |
| F13 | **Left-column restructure** | `ui.js` `drawUI` (big temp + day + city) | Big temp (sc4) + current-day label `DOW DD` (e.g. `SUN 18`, sc2) + city (sc1) stacked in the left column — replaces today's "big temp @110 + city @196 (sc2)". Uses F5 for the day-of-week. |
| F14 | **Three CURRENT INFO rows** | `ui.js` `drawUI` (rows) | Left column, **replacing the 3 bottom badges**: ① rain-state — glyph + `DRY` / `RAIN <amt>MM` / `SNOW <amt>MM` / `STORM`; ② wind — `rowWind` + daylight `lo->hi m/s` (F1+F6); ③ temp range — `rowTherm` + daylight `lo->hi°C` (F6; today's badge uses all-13h, v2 filters to daylight). |
| F15 | **2-day rain strip (v2.1)** | `ui.js` `drawTwoDayStrip` | Bottom area (x 240–786, y 212+): two day-blocks, each = day-of-week (sc2, F5) + **4 per-6h `stripIcon` medium icons** (F9; centers at `rightEdge-170+k*52`, pitch 52px, cy=SY+20 — v2.1; was 8 per-3h small icons, pitch 27px) + rain `DRY`/`<amt>MM` (left) + daylight `lo->hi°C` (right-aligned). Replaces the 3 bottom badges. Needs F3/F6. |
| F16 | **Moon phase on countdown** | `ui.js` `drawTempCurve` (night branch) | Draw the F10 glyph to the right of the **sunrise** (night→day) countdown label. |
| F17 | **Curve / layout parity** | `ui.js` vs `draw_temp_curve` | Cosmetic deltas for pixel parity: the "now" marker becomes a **filled dot** (web) vs the firmware's **hollow ring**; hour-axis knock-out y shifts with the left-column reflow (web 170/176 vs fw 178/184). Low priority. |

### E. Orchestration — `weather_test.ino` / `panel_state.cpp`

| # | Feature | What to port |
|---|---------|--------------|
| F18 | **Wire the new fetch** | `refresh_data()` calls `fetch_two_days()` + the 3-day daily (F2/F3) after `fetch_weather`; feed `draw_ui()` → `drawTwoDayStrip()`. |
| F19 | **(Optional) serial STATE preview** | Mirror the web's `&state=DRY\|RAIN\|SNOW\|STORM` with a serial `STATE` command so snow/storm can be previewed headless (the firmware has no such override today). |

---

## Porting constraints

- **No RTC.** The board has no real-time clock; `fetch_time()` is already a
  no-op by design. Every date-derived value — day-of-week (F5/F13/F15) and moon
  phase (F10) — must come from the **forecast's date fields** (F4 for current,
  F3 for the two future days), not `new Date()`/wall clock.
- **WMO code sets are shared.** Port the storm/snow/rain code lists verbatim
  from `smallIcon` + `rainState` so web and firmware classify identically.
- **Wind unit** is km/h from the API → display m/s (`/3.6`), consistent with the
  existing badge-2 behavior.
- **1-bit e-ink**: strip icons are the ~19–22px medium set (v2.1); keep them ≥ that
  size — the original ~9px small set was unreadable on the panel (see `defects.md`
  D14–D16 for what vanishes); row glyphs sit on paper, not on fills.
- **URL length:** the combined current+hourly+3-day-daily request path is ~365 chars —
  keep the `http_get` request buffer and the per-fetch `path` buffers at ≥ 512 (a
  `snprintf` truncation yields a silently malformed query → HTTP 400).

## Ordering vs the firmware refactor

`REFACTOR_PLAN.md` (split the 1055-line monolith into `panel_canvas/font/draw/
net/data/fetch/state/ui` + thin `.ino`) is **not started** — `weather_test.ino`
is still one file and no `panel_*.h/.cpp` exist yet. That plan explicitly
sequences this v2 UI work **after** the split, and each feature above already
names its module. Two options:

1. **(Recommended) Split first, then port.** Each F-lands in its named module
   (`panel_fetch.cpp`, `panel_draw.h`, `panel_ui.cpp`, …). Smaller diffs, easy
   review, matches the module map. Do `REFACTOR_PLAN.md` P0–P6, then F1–F19.
2. **Port into the monolith now**, skip the split. Faster to first v2 render but
   every change scrolls a 1055-line file; the split is still owed.

Either way, the **feature set and dependency order are the same.**

## Suggested build sequence (compile + flash after each)

1. **F1 + F2 + F4** — extend the existing `fetch_weather`/daily requests (wind,
   3-day daily, current date). Data lands in the already-present struct fields.
2. **F5 + F6** — pure helpers (no draw).
3. **F3 + F18** — `fetch_two_days` + wire into `refresh_data`.
4. **F8 + F9 + F11** — small icons + row glyphs (draw-only).
5. **F12** — `rainState`.
6. **F10 + F16** — moon phase + draw it on the countdown.
7. **F13 + F14** — left-column restructure + the three info rows (remove badges).
8. **F15** — `drawTwoDayStrip`.
9. **F17 + F19** — parity tweaks + optional STATE command.

## Verification gates

- **Hard:** clean `arduino-cli compile` (README command), successful flash,
  healthy boot log (`wx: OK …`, `TLS verified`, `panel up: … live`), and **no**
  `font: missing glyph` lines (every new label uses an existing glyph).
- **Golden CAP, same data window:** `tools/capture_weather_live.py` → decode
  base64 → `golden_v2.png`; send the same `MOCK hh:mm` to both the web
  (`?mode=live`/`MOCK`) and firmware, pixel-diff. Layout must match the v2 mock
  (numbers may differ live).
- **State coverage:** drive SNOW/STORM/RAIN via `&state=` (web) and the F19
  `STATE` command (firmware) to confirm the new glyphs + labels.
- **Parity:** the firmware render and `web/shot_v2_mock.png` match region-by-
  region (left rows, strip, moon phase).

## Summary

| Layer | Features |
|-------|----------|
| Fetch | F1 wind, F2 3-day daily, F3 2-day hourly, F4 current date, F5 dow, F6 daylight lo/hi |
| Font  | F7 (verify only — glyphs already present) |
| Draw  | F8 small icons, F9 `smallIcon`, F10 moon phase, F11 row glyphs |
| UI    | F12 rain state, F13 left column, F14 three info rows, F15 2-day strip, F16 moon on countdown, F17 parity |
| Orch  | F18 wire fetch, F19 (opt) STATE command |

**Net new:** ~2 data fetches + 2 struct fields wired, ~10 draw functions, the
rain-state classifier, the 2-day strip, and the left-column restructure.
**Already present:** glyphs 40–45, `WX`/`WXDay` struct fields, large icons.
