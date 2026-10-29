# ESP32-S3 E-Paper Panel — transport (direct-fetch travel page)

> Status: **rotary-switched page of the weather panel, deployed + live-verified
> on hardware** (same CrowPanel 5.79" device as
> [../epaper-panel-weather/](../epaper-panel-weather/README.md); Munich removed,
> so the panel is **Weather + Travel**, switched by the rotary **up/down
> detent** / canvas click; a press force-refreshes; a failed fetch shows the
> shared error page).
> Shows **live public-transport departures** from the **Karpatská** stop
> (Bratislava) for **line 32 → Dlhé diely**, **line 49 → Kramáre (NÚSCH)** and
> **line 44 → Koliba (cez Hrad)**.
> The **device fetches imhd.sk directly** over HTTPS (Socket.IO v4,
> `https://imhd.sk/rt/sio2/`, stop id 129) — **no LAN daemon** (the panel sits
> on a different subnet than any box, so a LAN daemon is unreachable). The 6 MB
> IDS BK GTFS zip never touches the device; the tools below still use imhd +
> GTFS for the full scheduled-hour cross-check and the web mockup's live mode.
> imhd.sk serves a **Sectigo** cert (not Let's Encrypt), so the TLS anchor set
> in `le_dercainfo.h` carries a 3rd root (LE ×2 for open-meteo + Sectigo ×1 for
> imhd). The device syncs its clock via **NTP** (`configTzTime`) to convert the
> imhd absolute-ms epochs to local time.
> Device facts + flash recipe: same as the weather panel.
> Data deep-dive: [../../epaper-panel-travel/docs/api-research.md](../../epaper-panel-travel/docs/api-research.md).

## Documents

- [../../epaper-panel-travel/README.md](../../epaper-panel-travel/README.md) —
  project overview, data-source table, quickstart (run the tools).
- [../../epaper-panel-travel/docs/api-research.md](../../epaper-panel-travel/docs/api-research.md) —
  the full API research: imhd Socket.IO v4 handshake + `tabs` payload field
  dictionary, the nastupiste↔GTFS sub-stop mapping, IDS BK GTFS schema +
  direction-on-trip, rejected alternatives, ToS.
- [../../epaper-panel-travel/TODO_PLAN.md](../../epaper-panel-travel/TODO_PLAN.md) —
  roadmap/status: data layer, web mockup, the LAN daemon, and the firmware page
  (now a module of the weather panel).

## Working data tools (in `epaper-panel-travel/tools/`)

- `imhd_realtime.py` — live feed fetcher (stdlib, EIO=4 handshake), CLI.
- `gtfs_next_hour.py` — GTFS scheduled next-hour (auto-downloads + caches the
  zip), CLI with `--at`/`--day`/`--horizon`/`--all`.
- `next_hour.py` — combined "next hour" (scheduled + live cross-check), CLI.
- `serve_next_hour.py` — optional LAN daemon (`GET /next-hour.json`, imhd + GTFS
  merged, background-cached) for the **web mockup's live mode**. The **device
  does NOT use it** — it fetches imhd.sk directly.
- `travel_config.py` — single source of truth for stop id, target lines, endpoints.

## Status (see TODO_PLAN)

- ✅ **Web e-ink mockup** (`web/panel.html`) — 800×272 1-bit, full-width header +
  three equal line columns (32 / 49 / 44) mirroring the weather `panel_v2`;
  mock + live modes, verified via PXL buffer dump + 2× PNG.
- ✅ **Web live feed** — optional LAN daemon (`tools/serve_next_hour.py`,
  `GET /next-hour.json`, imhd live + GTFS far-term, background-cached). Web-only;
  the device does not use it.
- ✅ **Firmware — device fetches imhd.sk directly** — a page of the weather
  panel: `../epaper-panel-weather/fw/weather_test/panel_travel.{h,cpp}`
  (Socket.IO v4 handshake + `tabs` parse, no daemon) + `panel_net.cpp`
  (generic HTTPS GET/POST + multi-CA TLS anchors) + the `panel_state` two-page
  machine + `weather_test.ino` (NTP sync + rotary toggle). **Pixel-identical to
  the web mock** (0 px / 217600 px); compiles clean (78% flash, 44% RAM).
- ✅ **Case** — shared with the weather CrowPanel.
- ✅ **Deployed + live-verified on hardware 2026-10-19** — flashed via
  `../epaper-panel-weather/tools/flash_test.sh` (COM4). On-device: imhd TLS
  verifies (Sectigo anchor), the Socket.IO handshake streams `tabs`, and the
  travel page renders live departures for 32 / 49 / 44 with `IN X` + live/late
  ticks (CAP framebuffer). Both pages (weather + travel) confirmed working.

## Firmware pitfalls (direct fetch — hard-won 2026-10-19)

- **`fill_rect` / `fill_circle` are INCLUSIVE** of their end coordinate. A full
  clear `fill_rect(0,0,800,272)` writes x=800 / y=272 → out of bounds →
  **`LoadStoreError` panic** on the real ESP32. The **host harness clamps OOB
  writes** (`cv_set` in `host/EPD.h`), so it renders fine and *masks the bug* —
  only the device crashes. Always clear with `fill_rect(0,0,EPD_W-1,EPD_H-1)`.
  `cv_set` in `panel_canvas.h` now also bounds-checks (device-side safety net;
  the host stub clamps the same way, so host↔web pixel-parity is preserved).
- **`Transfer-Encoding: chunked` is a per-RESPONSE flag, not a connection
  property.** The imhd sid/POST responses are `Content-Length` (NOT chunked);
  only some GETs are chunked. De-chunking must be **gated on the response
  header** — an unconditional chunk-split corrupts a body whose first byte is
  `0` (the Socket.IO `40` / `0{...}` frames) by reading a bogus `0`-length chunk.
- **imhd Socket.IO polling cadence:** GET sid → POST `40` (connect) → POST
  `42["tabStart",[129,["*"]]]` (subscribe) → **one** GET returns `40` + `42
  ["connect",...]` + `42["vInfo",...]` + `42["tabs",...]` in a single flush
  (split on `0x1e`). There is **no separate "40 ack" wait** — the server holds
  the POST response until it has events, so waiting for an early ack hangs.
- **imhd.sk serves a Sectigo cert** (leaf `*.imhd.sk` → `Sectigo Public Server
  Authentication CA DV R36` → `Sectigo Public Server Authentication Root R46`),
  not Let's Encrypt. Without the Sectigo root in `le_dercainfo.h` the TLS
  handshake fails with `r=-9984` (X509 verify). Regenerate via
  `tools/gen_le_dercainfo.py` (now multi-CA: LE ×2 + Sectigo ×1).
- **String-index UB:** `sid[sl++] = s[sl++]` (post-increment on both sides) is
  undefined behaviour and silently corrupted the sid on the device. Always
  write `sid[sl] = s[sl]; sl++;`.
- **`localtime_r` isn't portable** (missing on the host MinGW toolchain): use
  the `local_time()` wrapper in `panel_travel.cpp` (`localtime_r` on POSIX,
  `localtime_s` on Win32).
- **`strstr` cannot match diacritics** in the UTF-8 destination strings, so the
  line filter matches **diacritic-free** keywords (`diely`, `Kram`, `Koliba`)
  from `travel_config.py`.
