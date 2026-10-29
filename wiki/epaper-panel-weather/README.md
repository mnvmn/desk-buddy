# ESP32-S3 E-Paper Panel (implemented — bring-up)

> Status: **bring-up done.** The Elecrow CrowPanel 5.79" e-paper HMI
> (272×792, 1-bit, ESP32-S3) powers up, renders pre-rendered 1bpp bitmaps,
> joins Wi-Fi, and does a live TLS-verified weather fetch (Open-Meteo),
> showing the weather page **and a travel page** (rotary-switched, 2026-10-18;
> Munich removed). On a failed fetch a shared **error page** (warning triangle +
> source + message) is drawn instead of the old baked status bitmap.
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
  curve's "time to night" / "time to morning" transition labels each carry a
  live **moon-phase glyph** (synodic cycle from the 2000-01-06 new moon; lit =
  ink) — to the RIGHT of the sunset countdown, to the LEFT of the sunrise
  countdown. Same live/mock
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
  numbers): uniform vertical rhythm — ink gaps **19/19** between the three
  info rows (rows at y 16/49/82), **17** to the big temp (y 113), **20** to
  `DOW DD` (y 161), **19** to the city (y 194), **21** to the icon row
  (`ICY=236`, ink 218..248); top margin 16, bottom 22 — heavier elements get
  proportionally more air. This pass also fixed a firmware porting gap: the
  curve's hour labels lacked the web's clamp into `[CX0, CX1]`, so the edge
  labels stuck 12px out of the curve area.
  ✅ **Optical icon alignment 2026-10-18** (web `stripIcon` + firmware
  `strip_icon`): the 4 left-row icons (and the 2-day strip) align on the cloud
  BODY, not the bounding box — `mCloud` +3 / `mPart` −1 (its cloud is drawn 4px
  below its anchor, so −1 puts its body on the same centre line as a bare
  `mCloud`) / `mSun` 0 / `mRain` +4 / `mSnow` 0 / `mBolt` 0. Every cloud now
  shares one flat bottom + body centre and the sun ring centres on `ICY`.
  Host-verified pixel-identical after all passes (`tools/host_render.cpp`
  vs a live `?mode=mock` capture): **4 px total residual**, all on the
  rain-state drop glyph (pre-existing sub-glyph dither); icon row / info
  rows / strip / curve all 0 px. ✅ **Flashed 2026-10-18** (four times;
  `tools/flash_test.sh` COM4) and confirmed on-device: new layout + spacing +
  optical icon alignment live, golden re-captured to
  `tools/device_v24_capture.png` (+ `..._2x.png`; the device's left column is
  pixel-identical to the web mock, buffer rotated 180°).
  `web/shot_v2_mock.png` regenerated from the final web mock.

## Pages: Weather + Travel (2026-10-18)

The panel is a **two-page** app, switched by the **rotary up/down detent**
(firmware `toggle_page()`; web: click the canvas; serial `PAGE` for a headless
test). A **rotary press force-refreshes** the current page. `cur_page`
(0=weather, 1=travel) is persisted to NVS (`page`).

- **Munich removed.** The old two-city toggle (Bratislava | Munich) is gone —
  the weather page shows a single city (**Bratislava**). `CITIES` is a single
  entry and the serial `CITY` command became `PAGE`. The rotary **up/down
  detent** switches *page* (weather ↔ travel); a **press force-refreshes** it.
- **Travel page** (`fw/weather_test/panel_travel.h/.cpp`; web
  `web/panel_v2/travel_{data,icons,ui}.js`): three equal line columns
  (**32** Dlhé diely · **49** Kramáre NÚSCH · **44** Koliba) at Karpatská under
  a full-width header (stop + `DOW DD` + clock). Each row shows the scheduled
  time + an `IN nM` countdown + a late/live/scheduled tick. Data = imhd real-time
  feed (near term, ~45 min) merged with the IDS BK GTFS timetable (far term),
  served by the LAN daemon
  [`epaper-panel-travel/tools/serve_next_hour.py`](../../epaper-panel-travel/README.md)
  (`GET /next-hour.json`, default `0.0.0.0:8341`, background-cached so a GET is
  instant). The data pipeline, API research, and live/mock tools all live in
  `epaper-panel-travel/`.
- **Verification**: the firmware travel render is **pixel-identical to the web** —
  `tools/host_render_travel.cpp` (the real `panel_travel.cpp` on a stubbed 1bpp
  canvas) vs a live `?mode=mock&page=travel` capture: **0 px diff** over all
  217600 px. Firmware compiles clean (1,042,759 B = 79% flash, 87,628 B = 26% RAM).
- **Font**: travel labels are uppercase with diacritics folded, and avoid `X`/`Z`
  (missing from the firmware 5×7 font) — `DLHE DIELY`, `KRAMARE NUSCH`, `KOLIBA`.
- **Error page** (firmware `panel_error.h/.cpp`, web `error_ui.js`): a failed
  fetch draws a shared centered page — warning triangle + source (scale 4) +
  message (scale 4) + footer (scale 2). Weather: `WEATHER`/`NO WEATHER DATA`/
  `RETRYING` (data) or `NO CONNECTION`/`CHECK WIFI` (Wi-Fi down); travel:
  `KARPATSKA`/`NO TRAVEL DATA`/`DAEMON OFFLINE` (data) or `NO CONNECTION`/
  `CHECK WIFI`. Replaces the 26 KB `gImage_status` bitmap (saves flash);
  pixel-identical to the web (`tools/host_render_error.cpp` vs
  `?state=weather-data`): **0 px diff**.
- **Deployed + verified on hardware 2026-10-18**: flashed via
  `tools/flash_test.sh` (COM4). Confirmed the rotary up/down page toggle (live
  Open-Meteo weather), the travel page, and the travel error page (CAP
  framebuffer, 7462 ink px). The travel page refreshes on a 5-min cadence
  (`TRAVEL_REFRESH_MS`) vs the weather's 30-min (1 h at quiet hours); live travel
  data needs the daemon reachable from the panel's subnet — the panel is on
  `192.168.5.x` and the daemon target `192.168.4.23` is a different subnet, so
  the travel page shows the error page until they share a LAN.

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
