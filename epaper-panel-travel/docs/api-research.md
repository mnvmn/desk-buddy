# API research — public transport, Karpatská (Bratislava)

**Date:** 2026-10-18 · **Status:** both chosen sources verified against the
live servers. Everything below was reproduced by the tools in `tools/`.

We need **next-hour departures** from the Karpatská stop for **line 32 → Dlhé
diely** and **line 49 → Kramáre (NÚSCH)**. A stop board needs two things a
single source rarely gives: (a) accurate *near-term* times (with delay
corrections) and (b) the *full hour* of scheduled times. We use two feeds.

---

## 1. imhd.sk real-time feed (chosen — live source)

This is what the official
[virtual stop display](https://imhd.sk/ba/online-station-display-info) renders.
It is a **Socket.IO v4** server (engine.io v4) reachable over **plain HTTP
long-polling** — no WebSocket or extra libraries needed. Endpoint:
`https://imhd.sk/rt/sio2/`.

### Handshake (verified, stdlib `urllib`)

```
1  GET  https://imhd.sk/rt/sio2/?EIO=4&transport=polling
   -> 0{"sid":"...","upgrades":["websocket"],"pingInterval":30000,"pingTimeout":20000,"maxPayload":1000000}
       (engine.io OPEN; note EIO=4 — EIO=3 returns 400, the site moved on)

2  POST ...&EIO=4&transport=polling&sid=SID        body: "40"
       (engine.io MESSAGE '4' + socket.io CONNECT '0' to the default namespace)

3  GET  ...&sid=SID
   -> 40{"sid":"...}␞42["cack",true]
       (namespace connected; "cack" = client ack)

4  POST ...&sid=SID                                body: 42["tabStart",[129,["*"]]]
       (subscribe. [stop_id, [platforms…]] — "*" = all nástupíst)

5  GET  ...&sid=SID
   -> 42["vInfo",{vehicle…}]␞42["vInfo",{…}]␞…␞42["tabs",[ {nastupiste, tab:[…]} , … ]]
```

Notes:
- One HTTP read can return **multiple** engine.io messages, separated by byte
  **0x1e** (␞). Each socket.io EVENT is `42["name", payload]`.
- Send `Accept-Encoding: identity` (no gzip) so the 0x1e split is clean.
- The server periodically emits an engine.io PING (`2`) — answer with a PONG
  (`3`). Over a ~2–3 s fetch you'll usually see 0–1.
- `vInfo` events are vehicle metadata (type, plate) — not needed for a
  departure list. The event we want is **`tabs`**.

### `tabs` payload shape

`tabs` → a **list**, one entry per **nástupiste (platform)**:

```jsonc
[
  { "zastavka": 129, "nastupiste": 312, "timestamp": 1792327653601, "nextRowAt": null,
    "tab": [
      { "linka": "61", "issi": "1:6719", "sekcie": ["ba"],
        "cas": 1792328325000,      // ms — LIVE predicted departure
        "casCP": 1792328340000,    // ms — SCHEDULED (cestovný poriadok) departure
        "casDelta": 0,             // sec — live − scheduled
        "typ": "online",           // "online" = real-time tracked; "cp" = schedule only
        "cielStr": "Letisko / Airport",   // destination shown on board
        "konecnaZstr": "Letisko / Airport",
        "odjazd": "11 min" }       // human-readable countdown
      // …more departures…
    ] }
  // …one more object per platform (313, 314, 315)…
]
```

Field dictionary (the ones we use):

| field | meaning |
| --- | --- |
| `linka` | line number (`"32"`, `"49"`, `"61"`…); `"►"` = vehicle currently at the stop |
| `cas` | **live** predicted departure, epoch **ms** |
| `casCP` | **scheduled** departure, epoch ms (cestovný poriadok) |
| `casDelta` | seconds, `cas − casCP` (live delay) |
| `typ` | `"online"` (real-time) or `"cp"` (schedule only) |
| `cielStr` / `konecnaZstr` | destination / terminal station text (for direction) |
| `odjazd` | board countdown text |
| `nastupiste` | platform id (top-level, per table) |

### Stop / platform mapping (Karpatská = stop id 129)

Karpatská has **4 nástupíst**. imhd `nastupiste` ↔ GTFS sub-stop:

| imhd nastupiste | GTFS stop_id | serves (by direction) |
| --- | --- | --- |
| 312 | `000000014400001` | 61→Hl.st., 40→SND, 71→Vrakuňa, **32→Jelačičova**, **49→Mliekarenská**, 44→Hrad |
| 313 | `000000014400002` | 61→Hl.st., 40→Hl.st., 71→Hl.st., **32→Dlhé diely**, **49→Kramáre** |
| 314 | `000000014400003` | 44→Hlavná stanica |
| 315 | `000000014400004` | 44→Koliba |

→ `nastupiste = 311 + (last GTFS sub-stop digit)`. **Both target lines leave
from nástupiste 313 / sub-stop 2.** That makes the panel trivially scoped:
read platform 313, keep `linka ∈ {32,49}` and the right `cielStr`.

Direction filter (from the live feed):
- **32 → Dlhé diely:** `cielStr` contains `Dlhé diely` (board text `Hl. stanica►Dlhé diely`)
- **49 → Kramáre:** `cielStr` contains `Kramáre` (board text `Kramáre, NÚSCH` / `Kramáre, NOÚ`)

### Limits
- Only shows what's **actively approaching** (+ a few scheduled) — in practice
  ~30–45 min out, not a clean full hour. Hence the GTFS feed below.

---

## 2. IDS BK GTFS (chosen — scheduled source)

[IDS BK open data](https://www.idsbk.sk/system/open-data/) publishes the
Bratislava city transit timetable as a **GTFS** zip (ArcGIS-hosted):
`https://www.arcgis.com/sharing/rest/content/items/aba12fd2cbac4843bc7406151bc66106/data`
(~6.3 MB). It is **static** (a timetable snapshot; re-download ~daily) but has
the **entire day**, so it cleanly covers "the next hour."

Verified contents (relevant files): `routes.txt`, `trips.txt`, `stop_times.txt`,
`stops.txt`, `calendar.txt`, `calendar_dates.txt`.

- **Stop:** `stops.txt` → `stop_code = 144` = "Karpatská", with 4 sub-stops
  (`…01…04`) = the 4 nástupíst above.
- **Routes:** `route_short_name` `32` and `49` both present (`agency 01`,
  `route_type 11`). The passenger direction is on the **trip**, not the route.
- **Direction:** on `trips.txt` — `direction_id` + `trip_headsign`.
  - 32 → Dlhé diely: `direction_id 1`, headsign `Dlhé diely`
  - 32 → Jelačičova: `direction_id 0`, headsign `Jelačičova`
  - 49 → Kramáre: `direction_id 1`, headsign `NÚSCH` / `Nár. onkol. ústav`
  - 49 → Mliekarenská: `direction_id 0`, headsign `Mliekarenská` / `Trnavské mýto`
- **Day type:** `calendar.txt` (weekday flag) + `calendar_dates.txt` (date
  exceptions). 2026-10-18 (Sunday) → active services include `Nedela+sv_3`.
- **Time:** `stop_times.departure_time` at the Karpatská `stop_id`, `HH:MM:SS`
  (local). Some rows only carry `arrival_time`.

### Verified: GTFS next-hour matches live + the imhd CP schedule

2026-10-18 14:50, both lines on a 15-min pattern:

| +min | L32 → Dlhé diely | L49 → Kramáre |
| --- | --- | --- |
| +04 | 14:55 | 14:48 (just left) |
| +13 | | 15:03 |
| +20 | 15:10 | |
| +28 | | 15:18 |
| +35 | 15:25 | |
| +43 | | 15:33 |
| +50 | 15:40 | |
| +58 | | 15:48 |

These match the imhd live feed (to ~15 s) **and** the imhd
"cestovný poriadok" free-day table (15:03/15:18/15:33/15:48; 14:55/15:10/…).
GTFS also reaches **15:48 / 15:55**, which the live feed hadn't emitted yet.

---

## 3. imhd "cestovný poriadok" (CP) — the static timetable pages

The linked schedule pages (e.g.
[linka/32](https://imhd.sk/ba/cestovny-poriadok/linka/32/Karpatsk%C3%A1/smer-Dlh%C3%A9-diely-cez-Hlavn%C3%BA-stanicu/ca71bd718971807c807f847f82817c8281717b71c17189807b71b671898081887b71be71897fcc),
[linka/49](https://imhd.sk/ba/cestovny-poriadok/linka/49/Karpatsk%C3%A1/smer-Kram%C3%A1re-N%C3%A1rodn%C3%BD-%C3%BAstav-srdcov%C3%BDch-a-cievnych-chor%C3%B4b-Kram%C3%A1re-N%C3%A1rodn%C3%BD-onkologick%C3%BD-%C3%BAstav/ca71bd718971807c807f847f83887c8388717b71c17189807b71b671898081887b71be71897fcc))
show the full timetable split into **Pracovné dni (školský rok / prázdniny)**
and **Voľné dni**. They're server-rendered HTML (no clean JSON), so scraping is
fragile. **We use GTFS instead** — same data, machine-readable, and it's the
official open-data export of the same timetable. The CP pages are kept here
only as the human ground-truth for cross-checking.

---

## 4. Alternatives considered (and why not)

| Option | Why not |
| --- | --- |
| **node-imhdsk / python-imhdsk-api** (npm/PyPI) | Old; target `EIO=3` handshake, now returns 400. Our stdlib client speaks current EIO=4. No dependency needed. |
| **OpenMobilityData / Mobility DB** | Aggregates GTFS-RT from operators; unreliable coverage for Bratislava, and adds an account dependency. IDS BK doesn't expose GTFS-RT publicly. |
| **GTFS-RT (realtime vehicles/trips)** | IDS BK open data publishes **static GTFS only** (no GTFS-RT). Real-time therefore comes from the imhd feed, not GTFS-RT. |
| **Mapbox / TomTom / HERE transit** | Third-party, key + cost, and resells the same schedule; overkill for a 2-line stop board. |
| **HAFAS** (imhd's backend is HAFAS-based) | The HAFAS REST/SOAP endpoints are the *source* of imhd, but undocumented/protected; the imhd Socket.IO feed is the sanctioned real-time surface. |

**ToS / licensing:** the imhd virtual-display
[terms](https://imhd.sk/ba/online-station-display-info) permit **personal,
non-commercial** use of the realtime data — exactly this project. GTFS open
data is published for machine processing. Keep polling modest (a stop board
refreshes every ~30–60 s; a background fetch, not a hammer).

---

## 5. How the pieces fit (target architecture)

```
                 ┌──────────────┐   ~daily    ┌──────────────────┐
  (scheduled)    │ IDS BK GTFS  │ ──────────▶ │  data/idsbk_*.zip│
                 └──────────────┘  download   └────────┬─────────┘
                                                       │ next 60 min
                                                       ▼
                 ┌──────────────┐  ~30–60 s   ┌──────────────────┐   ┌─────────┐
  (live)         │ imhd /rt/sio2│ ──────────▶ │  merge: schedule  │──▶│ e-ink   │
                 └──────────────┘             │ + live deltas on │   │ panel   │
                                              │ near-term rows   │   └─────────┘
                                              └──────────────────┘
```

- **GTFS** → the backbone "next 60 min" list (stable, complete).
- **imhd live** → replaces/annotates the next ~15–20 min with real-time times
  + delay deltas; also flags a vehicle as "at the stop" (`linka == "►"`).
- Both scoped to **nástupiste 313 / sub-stop 2**, `linka ∈ {32, 49}`, correct
  direction.

Implementation in `tools/` already proves each half independently
(`gtfs_next_hour.py`, `imhd_realtime.py`); `next_hour.py` shows both side by
side. The web mockup + firmware will render the merged view.
