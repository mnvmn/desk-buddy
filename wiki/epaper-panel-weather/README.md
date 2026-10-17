# ESP32-S3 E-Paper Panel (implemented — bring-up)

> Status: **bring-up done.** The Elecrow CrowPanel 5.79" e-paper HMI
> (272×792, 1-bit, ESP32-S3) powers up, renders pre-rendered 1bpp bitmaps,
> joins Wi-Fi, and does a live TLS-verified weather fetch (Open-Meteo),
> showing the weather page (net-status page as offline fallback).
> Current firmware: `fw/weather_test` — live weather: fetches Open-Meteo and
> draws the **v2 layout** in C (`draw_ui`): 4 left icons + big temp + `DOW DD`
> day + city, three info rows (rain-state / daylight wind / daylight temp),
> a 13h curve with night band + a moon-phase "till night" countdown, and a
> 2-day rain strip. `fw/test_sketch` is the earlier bring-up mock.
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
  icons, one per 6h block** (pitch 52px) on line 1, rain amount + daylight-only
  lo-hi °C on line 2 (the original 8 small per-3h icons were too small to read
  on the panel); the old bottom badges move
  into the left column (larger sc2, even spacing) alongside the current day.
  Left-column rows: a rain STATE (DRY / RAIN / SNOW / STORM, from the 13h WMO
  `weather_code` — storm & snow get procedural bolt/snowflake icons) + a
  daylight wind interval (lo-hi m/s) + a daylight temp range (lo-hi °C). The
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
  features F1–F19, verified on hardware); feature list + build order +
  verification: `epaper-panel-weather/V2_UI_PORT_PLAN.md`. ⚠️ The v2.1 strip
  change (4 medium per-6h icons, 2026-10-17) is **not yet in the firmware** —
  the port plan carries it as a pending re-port.

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
