# ESP32-S3 E-Paper Panel (implemented — bring-up)

> Status: **bring-up done.** The Elecrow CrowPanel 5.79" e-paper HMI
> (272×792, 1-bit, ESP32-S3) powers up, renders pre-rendered 1bpp bitmaps,
> joins Wi-Fi, and does a live TLS-verified weather fetch (Open-Meteo),
> showing the weather page (net-status page as offline fallback).
> Current firmware: `fw/weather_test` — live weather: fetches Open-Meteo and
> draws the **v2 layout** in C (`draw_ui`): three info rows at the top of the
> left column (daylight temp / daylight wind / rain-state), big temp + `DOW DD`
> day + city, and the 4-icon row at the bottom of the left column,
> a 13h curve with night band + a moon-phase "till night" countdown, and a
> 2-day rain strip. **Code layout (split from the 1380-line monolith
> 2026-10-17, verified pixel-identical on live data):** `weather_test.ino`
> is control flow only (setup/loop/serial commands); everything else lives
> in `panel_*.h/.cpp` modules — `panel_canvas` (framebuffer + layout
> constants), `panel_font` (5×7 glyphs), `panel_draw` (primitives + icons),
> `panel_data` (data model), `panel_net` (mbedTLS `http_get`), `panel_fetch`
> (Open-Meteo fetch), `panel_state` (app state + boot/serial loop),
> `panel_ui` (`draw_ui` + status page).
> Device facts + flash recipe:
> [../../epaper-panel-weather/README.md](../../epaper-panel-weather/README.md).
> Shared docs: [../shared/](../shared/); sibling device (C6 AMOLED):
> [../amoled-panel-llama/](../amoled-panel-llama/).

## Documents

- [bringup.md](bringup.md) — flash + verify results with serial ground
  truth (blank test image → 12h weather mock → net-status page → live
  HTTPS weather), EDP library notes, panel geometry gotchas, 1-bit
  rendering pitfalls
- [defects.md](defects.md) — the 12h weather mock defect log D1–D16 (all fixed
  & verified: what vanishes on 1-bit e-ink and why) + D17 (live-firmware font
  drops `I`/`J` — fixed & flashed 2026-10-17)
- `web/panel_v2.html` — the web mockup of the panel (v2 layout): a canvas
  1-bit e-ink render at true 800×272 (shown 2× pixelated), fetching Open-Meteo
  live with a baked-in mock fallback. `?mode=mock` forces the offline path
  (for screenshots); `?mode=live` forces a fetch. The bottom row is a 2-day
  rain strip — v2.1 (2026-10-17): each day = day-of-week name + **4 medium
  icons — the dominant condition of each 6h block** (`blockCode`: precipitation
  always wins, otherwise most-frequent dry code; pitch 52px) on line 1, rain
  amount + daylight-only lo-hi °C on line 2 (the original 8 small per-3h point
  samples were too small to read and missed mid-block weather); the old bottom
  badges move
  into the left column (larger sc2, even spacing) alongside the current day.
  **Left icon row (v2.1):** the four condition icons at the **bottom of the
  left column** are the **4 equal parts from now to midnight**, each via the
  same `blockCode`+`stripIcon` as the
  strip — **identical logic to the bottom row** (replaced point samples at
  now/+3/+6/+9h and the day/night sun-moon split; rain/snow/storm are visible
  in that row now).
  Left-column rows (top to bottom, single layout swap 2026-10-18 moved them
  from the bottom to the top and the icon row from the top to the bottom): a
  daylight temp range (lo-hi °C) + a
  daylight wind interval (lo-hi m/s) + a rain STATE (DRY / RAIN / SNOW / STORM,
  from the 13h WMO
  `weather_code` — storm & snow get procedural bolt/snowflake icons). The
  curve's "time to night" label carries a live **moon-phase glyph** on its
  right (synodic cycle from the 2000-01-06 new moon; lit = ink). Same live/mock
  (`?mode=mock|live`; `&state=DRY|RAIN|SNOW|STORM` previews a state); daylight
  lo-hi uses sunrise/sunset. Extends the firmware 5×7 font (indices 0-39) with
  extras — `°` (41), `m` (44), `s` (45) plus separator candidates `~`/`>`/`_`
  (40/42/43); the range separator defaults to the dash `-` (index 33). The
  **code is split into modules** under `web/panel_v2/` (canvas / font / icons /
  ui / data / app, loaded as classic ordered `<script>` tags because `file://`
  blocks ES modules). `panel_live.html` /
  `panel_annotated.html` were the earlier single-page mockups, removed in the
  2026-10-17 web split. **In firmware** — ported 2026-10-17 (all 19
  features F1–F19, verified on hardware; the full feature list / build order /
  verification gates are in git history — `V2_UI_PORT_PLAN.md`, deleted
  2026-10-17 with its pending work moved to
  `epaper-panel-weather/TODO_PLAN.md`). ✅ **v2.1 changes ported 2026-10-17**:
  strip → 4 medium per-6h icons and left icon row → 4 equal now→midnight
  parts, both via `block_code`+`strip_icon`, plus the 24h fetch
  (`forecast_hours=24`, `codes24[24]`). Host-verified pixel-identical on the
  v2.1 icon rows (`tools/host_render.cpp` vs a live `?mode=mock` capture:
  left row 0 px, 2-day strip 0 px; the ESP32 firmware compiles clean).
  ✅ **Layout swap 2026-10-18** (web + firmware): the three info rows now sit
  at the **top** of the left column (temp range / wind / rain-state) and the
  4-icon row at the **bottom** (single row). Also fixed a pre-existing firmware
  typo: the wind row printed `ms` instead of `m/s` (missing `/` glyph).
  ✅ **Left-column spacing/rhythm pass 2026-10-18** (web + firmware, same
  numbers): uniform vertical rhythm — ink gaps **17/17** between the three
  info rows (rows at y 32/62/92), **15** to the big temp (y 120), **18** to
  `DOW DD` (y 165), **17** to the city (y 195), **21** to the icon row
  (`ICY=239`, ink 222..251); top margin 32, bottom 21 — heavier elements get
  proportionally more air. This pass also fixed a firmware porting gap: the
  curve's hour labels lacked the web's clamp into `[CX0, CX1]`, so the edge
  labels stuck 12px out of the curve area.
  Host-verified pixel-identical after both passes (`tools/host_render.cpp`
  vs a live `?mode=mock` capture): **4 px total residual**, all on the
  rain-state drop glyph (pre-existing sub-glyph dither); icon row / info
  rows / strip / curve all 0 px. ✅ **Flashed 2026-10-18** (twice;
  `tools/flash_test.sh` COM4) and confirmed on-device: new layout + spacing
  live, golden re-captured to `tools/device_v23_capture.png` (+ `..._2x.png`;
  the device's left column is pixel-identical to the web mock, buffer rotated
  180°). `web/shot_v2_mock.png` regenerated from the final web mock.

## Key gotchas (details in the docs)

- **Panel geometry**: two cascaded SSD1683s with an 8-column dead zone at
  the splice — full-screen buffers are 800×272; image data is 792×272
  (mod-8 rows), 1bpp row-major MSB-first.
- **1-bit only**: any color with luminance > 128 vanishes (dither grays
  with a checkerboard); draw icons on dark fills in the light color
  `(235,238,242)`.
- **TLS**: this Arduino core's PEM parser rejects the Let's Encrypt
  chain — feed DER anchors (`le_dercainfo.h`); chunked responses must be
  de-chunked before parsing.
- **Flashing**: COM4; after `arduino-cli upload` the board sits in
  download mode — exit with the DTR low / RTS pulse, then look for the
  `panel up:` serial line.
