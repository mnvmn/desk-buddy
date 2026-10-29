# epaper-panel-travel — CrowPanel 5.79" E-Paper: next public transport

Desk-buddy target-device folder (same hardware as
[`epaper-panel-weather/`](../epaper-panel-weather/README.md): Elecrow
**CrowPanel ESP32 5.79" e-paper HMI**, 272×792 B/W, ESP32-S3).

**Goal:** a glanceable e-ink board showing the **next hour of public transport
departures** from the **Karpatská** stop (Bratislava), for three lines:

| Line | Direction | Destination |
| --- | --- | --- |
| **32** | Dlhé diely (cez Hlavnú stanicu) | [imhd.sk link](https://imhd.sk/ba/cestovny-poriadok/linka/32/Karpatsk%C3%A1/smer-Dlh%C3%A9-diely-cez-Hlavn%C3%BA-stanicu/ca71bd718971807c807f847f82817c8281717b71c17189807b71b671898081887b71be71897fcc) |
| **49** | Kramáre (NÚSCH / NOÚ) | [imhd.sk link](https://imhd.sk/ba/cestovny-poriadok/linka/49/Karpatsk%C3%A1/smer-Kram%C3%A1re-N%C3%A1rodn%C3%BD-%C3%BAstav-srdcov%C3%BDch-a-cievnych-chor%C3%B4b-Kram%C3%A1re-N%C3%A1rodn%C3%BD-onkologick%C3%BD-%C3%BAstav/ca71bd718971807c807f847f83887c8388717b71c17189807b71b671898081887b71be71897fcc) |
| **44** | Koliba (cez Hrad) | [imhd.sk link](https://imhd.sk/ba/cestovny-poriadok/linka/44/%C3%9Arad-vl%C3%A1dy-SR/smer-Koliba-cez-Hrad/ca71bd718971807c807f847f83837c8383717b71c171897f7b71b6718984867f7b71be71897fcc) |

Stop page: [imhd.sk/ba/zastavka/Karpatská](https://imhd.sk/ba/zastavka/Karpatsk%C3%A1/b6808188)

All three run a **15-minute pattern**. Lines **32** and **49** leave from
**nástupiste 313** (GTFS sub-stop 2); line **44** leaves from **nástupiste 315**
(GTFS sub-stop 4).

## Data sources (what actually works — verified 2026-10-18)

Two complementary feeds cover everything; neither alone is enough.

| | **imhd.sk real-time** | **IDS BK GTFS** |
| --- | --- | --- |
| What | Live next departures (vehicles actually approaching) | Static timetable (whole day) |
| Reach | ~30–45 min out, then stops | The full day |
| Accuracy | **Live** — corrects for delays (`casDelta`) | Scheduled (on-time) |
| Transport | Socket.IO v4 over HTTP long-poll (`/rt/sio2/`) | One static GTFS zip (ArcGIS) |
| Endpoint | `https://imhd.sk/rt/sio2/` | [idsbk.sk open data](https://www.idsbk.sk/system/open-data/) |

**Design:** use **GTFS** to lay out the full next hour, and **imhd real-time**
to correct the near-term entries (the panel will show the scheduled times and
overlay the live deltas on the next ~15–20 min). Cross-check on 2026-10-18
(Sunday): both agree to within ~15 s.

> Full protocol details, the Socket.IO handshake, the GTFS schema mapping,
> the imhd CP page, licensing/ToS, and rejected alternatives: see
> **[`docs/api-research.md`](docs/api-research.md)**.

## Quickstart

No dependencies beyond CPython (stdlib only). GTFS is auto-downloaded to
`data/idsbk_gtfs.zip` on first run (gitignored).

```
cd epaper-panel-travel/tools
python next_hour.py                 # next 60 min, all three lines, scheduled + live
python imhd_realtime.py             # live feed only (all target lines)
python gtfs_next_hour.py --at 14:50 # scheduled only, from a given time
```

Sample (2026-10-18 15:52, Sunday / voľné dni):

```
 NEXT HOUR (scheduled)
   +03m  15:55   L32   → Dlhé diely (cez Hlavnú stanicu)
   +11m  16:03   L49   → Kramáre (NÚSCH / NOÚ)
   +11m  16:04   L44   → Koliba (cez Hrad)
   +18m  16:10   L32   → Dlhé diely (cez Hlavnú stanicu)
   +26m  16:18   L49   → Kramáre (NÚSCH / NOÚ)
   +26m  16:19   L44   → Koliba (cez Hrad)
   +33m  16:25   L32   → Dlhé diely (cez Hlavnú stanicu)
   +41m  16:33   L49   → Kramáre (NÚSCH / NOÚ)
   +41m  16:34   L44   → Koliba (cez Hrad)
   +48m  16:40   L32   → Dlhé diely (cez Hlavnú stanicu)
   +56m  16:48   L49   → Kramáre (NÚSCH / NOÚ)
   +56m  16:49   L44   → Koliba (cez Hrad)
```

## Web mockup

**[`web/panel.html`](web/panel.html)** is the e-ink design target. Open it in a
browser (mock data is baked in; add `?mode=live` behind a local server once the
daemon writes `data/panel_live.json`). It reuses the weather panel's design
system exactly — same 800×272 1-bit `INK`/`PAPER` buffer, same 5×7 uppercase
font (diacritics folded, e.g. `KARPATSKA`, `DLHE DIELY`), same dark-bezel
chrome. Modules in `web/panel/`: `canvas.js` + `font.js` (copied verbatim),
`icons.js` (bus, clock, status ticks), `ui.js` (header + three line columns), `data.js`
(live fetch + baked mock), `app.js` (boot + status pill).

## Layout

```
epaper-panel-travel/
├─ README.md            this file
├─ TODO_PLAN.md         open work / roadmap
├─ docs/
│  └─ api-research.md   data-source research (the "which API" deep dive)
├─ tools/               working, stdlib-only data tools
│  ├─ travel_config.py  single source of truth: stop id, targets, endpoints
│  ├─ imhd_realtime.py  live feed (Socket.IO v4) fetcher + CLI
│  ├─ gtfs_next_hour.py GTFS scheduled next-hour + CLI
│  ├─ next_hour.py      combined next-hour overview (scheduled + live)
│  └─ serve_next_hour.py LAN JSON endpoint for the e-paper (imhd+GTFS merged)
├─ data/                cached GTFS zip (gitignored)
├─ web/                 standalone mockup web/panel.html; SHIPPED UI = the weather
│                        app page ../epaper-panel-weather/web/panel_v2/travel_*.js
├─ fw/                  (empty) — the e-paper travel page now lives in the weather
│                        project: ../epaper-panel-weather/fw/weather_test/panel_travel.{h,cpp}
└─ case/  lib/          3D-printed case / display driver (shared w/ weather)
```

## Device

Same hardware as the weather panel (Elecrow CrowPanel ESP32 5.79", SSD1683
×2, 272×792 B/W, ESP32-S3, COM4 on this machine). Full device spec, EPD SPI
pin map, and flashing instructions live in
[`../epaper-panel-weather/README.md`](../epaper-panel-weather/README.md) —
this panel reuses that bring-up.

## Status

- ✅ Project initialized; data layer researched **and proven** (all three tools
  return correct live + scheduled times for all three lines).
- ✅ README + API research + TODO plan.
- ✅ Web e-ink mockup (`web/panel.html`) — full-width header + three equal line
  columns (32 / 49 / 44), matching the weather panel's `panel_v2` (1-bit buffer,
  5×7 font, dark-bezel chrome).
- ✅ **LAN daemon** (`tools/serve_next_hour.py`) — serves `GET /next-hour.json`
  (imhd live near-term + GTFS far-term, background-cached; default `0.0.0.0:8341`).
  This is the data source the e-paper firmware **and** the web live mode both fetch.
- ✅ **Integrated into the weather panel** as a click-to-toggle page — web
  `../epaper-panel-weather/web/panel_v2/travel_{data,icons,ui}.js` + firmware
  `../epaper-panel-weather/fw/weather_test/panel_travel.{h,cpp}`. **Munich removed**;
  the panel is now **Weather + Travel**, toggled by the rotary **up/down
  detent** (firmware) / canvas click (web) / serial `PAGE` (a press
  force-refreshes). A failed fetch draws the shared **error page** (no mock
  fallback). The firmware travel render is **pixel-identical to
  the web** (0 px over 217600 px) and compiles clean (79% flash, 26% RAM).
- ✅ **Deployed + verified on hardware 2026-10-18** — flashed via
  `../epaper-panel-weather/tools/flash_test.sh` (COM4); the travel page and the
  shared error page both confirmed on-device (CAP framebuffer).

See [`TODO_PLAN.md`](TODO_PLAN.md) and
[`wiki/epaper-panel-travel/`](../wiki/epaper-panel-travel/).

## References

- imhd.sk virtual stop display + ToS: <https://imhd.sk/ba/online-station-display-info>
- IDS BK open data (GTFS): <https://www.idsbk.sk/system/open-data/>
- GTFS (city transit, ArcGIS): <https://www.arcgis.com/sharing/rest/content/items/aba12fd2cbac4843bc7406151bc66106/data>
- GTFS spec: <https://gtfs.org/>
