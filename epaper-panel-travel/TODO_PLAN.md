# TODO plan — epaper-panel-travel

**Updated:** 2026-10-18 · Open work for the CrowPanel 5.79" e-paper
**transport** panel (Karpatská, lines 32 + 49 + 44, next-hour departures).
Hardware bring-up is inherited from `epaper-panel-weather` (same device).

## 0. Data layer — DONE (2026-10-18)

Both sources researched **and proven** against the live servers (see
`docs/api-research.md`). Working stdlib tools in `tools/`:
- `travel_config.py` — stop id (129), target lines, endpoint constants.
- `imhd_realtime.py` — live Socket.IO v4 feed (verified: 32/49 departures).
- `gtfs_next_hour.py` — GTFS scheduled next-hour (verified: matches live + CP).
- `next_hour.py` — combined scheduled + live overview.
Cross-check 2026-10-18 (Sunday): live and GTFS agree to ~15 s; both match the
imhd "cestovný poriadok" free-day table.

## 1. Web e-ink mockup — DONE (2026-10-18)

`web/panel.html` + `web/panel/*.js`, mirroring `epaper-panel-weather/web/panel_v2/`:
- 1-bit (INK/PAPER) 800×272 buffer; `canvas.js`/`font.js` copied verbatim.
- **Layout (built):** full-width header (bus + KARPATSKA | day + time + clock);
  body split into THREE equal line columns (32 → DLHE DIELY / 49 → KRAMARE NUSCH
  / 44 → KOLIBA), each with a header (big line number + destination + nástupiste
  N 313 / N 315) and the next-hour rows: time (sc3), `>` next marker, status tick
  (■ live / □ sched / ▲ late), and `IN Nm` (sc1).
- `?mode=mock` (baked 2026-10-18 data) + `?mode=live` (fetch
  `data/panel_live.json`).
- Verified via PXL buffer dump + 2× PNG; all text legible, nothing cut off.
- [ ] Polish after screenshot review (row spacing, secondary-text size,
  status-mark visibility).

## 2. Firmware port — DONE (2026-10-18, in the weather project)

The travel page is a **module of the weather panel**, not a separate firmware.
It lives in `../epaper-panel-weather/fw/weather_test/`:
- `panel_travel.h/.cpp` — data model + `parse_travel` (hand-rolled JSON, same
  style as `panel_fetch`) + `draw_travel` (three-column layout ported from the
  web `travel_ui.js` / `travel_icons.js`).
- `panel_state.h/.cpp` — the app is now a **two-page** state machine
  (`cur_page`: 0=weather, 1=travel); `toggle_page()` switches + persists to NVS.
  **Munich removed** (single city Bratislava); the old `toggle_city` is gone.
- `weather_test.ino` — rotary **up/down detent** toggles the **page** (not
  city); a **press force-refreshes**; serial `PAGE` for headless testing; travel
  refresh cadence `TRAVEL_REFRESH_MS` (5 min). A failed fetch draws the shared
  **error page** (`panel_error.h/.cpp`) — no mock fallback.
- The "GTFS too big for the device" question above resolved as **(a): a host
  daemon** — see §3.
- **Verified**: the firmware travel render is **pixel-identical to the web** —
  `../epaper-panel-weather/tools/host_render_travel.cpp` (the real
  `panel_travel.cpp` on a stubbed 1bpp canvas) vs a live `?mode=mock&page=travel`
  capture: **0 px** over 217600 px. Compiles clean (79% flash, 26% RAM).

## 3. Packaging / ops — DONE (2026-10-18)

- **Host daemon** = `tools/serve_next_hour.py` (stdlib only):
  - keeps `data/idsbk_gtfs.zip` fresh (`gtfs_next_hour.ensure_gtfs`, 24 h TTL),
  - fetches the imhd live feed in a background thread every 60 s,
  - serves `GET /next-hour.json` on `0.0.0.0:8341` from a background **cache**
    (a GET returns in <10 ms — safe for the web's 2.5 s live-fetch timeout and
    the ESP32's 10 s fetch budget alike).
  - Merge: imhd live near-term (~45 min) + GTFS far-term to the full hour,
    de-duped by time (live wins), capped at 4 rows/line.
- **Refresh cadence**: daemon cache 60 s; panel travel page 5 min
  (`TRAVEL_REFRESH_MS`); e-ink uses the fast partial-refresh (no per-minute
  full refresh).
- `case/` + `lib/EDP` shared with the weather panel.
- ✅ **Deployed + verified on hardware 2026-10-18**: flashed via
  `../epaper-panel-weather/tools/flash_test.sh` (COM4); the travel page + the
  shared error page both confirmed on-device (CAP framebuffer, 7462 ink px).

## Constraints / gotchas (from research)

- **EIO=4**, not 3 (the 2019 `node-imhdsk` handshake is dead — 400).
- engine.io messages in one read are 0x1e-delimited; socket.io EVENT = `42[...]`.
- Send `Accept-Encoding: identity`; answer engine.io PING (`2`) with PONG (`3`).
- imhd `tabs` = a **list** of per-platform tables (nástupiste 312–315); both
  target lines are on **313**. GTFS sub-stop **2** = nastupiste 313.
- GTFS is **static** — the "next hour" is scheduled; live deltas come from imhd.
- Direction is on the **trip** (headsign + direction_id), not the route.
- imhd ToS = personal/non-commercial only — fine for this panel; keep polling
  modest.

## References

- README: `README.md` · API deep-dive: `docs/api-research.md`
- Weather panel (device + mockup + firmware conventions): `../epaper-panel-weather/`
- wiki: `../wiki/epaper-panel-travel/`
