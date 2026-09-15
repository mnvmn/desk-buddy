# DeskBuddy — Product Spec

A desk buddy: a **Waveshare ESP32-C6-Touch-AMOLED-2.16** (2.16" square
AMOLED, **480×480**, capacitive touch, 16 MB flash, TF-card slot, IMU,
RTC, audio, LiPo) that watches a local `llama.cpp` (`llama-server`)
instance and shows its live generation speed.

Status: draft v0.2 (hardware confirmed against Waveshare's bundled code)
Owner: —
Last updated: 2026-09-14
Source: <https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16> +
`github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16`

---

## 1. Goal

Put a small always-on display on the desk that answers one question at a
glance: **is my local model generating right now, and how fast?**

Two things it does:

1. **Live mode** (default): a large token/second readout, updated in real
   time while the model is generating.
2. **Screensaver mode** (one button press): stops the numbers and shows a
   picture instead — **one picture while working**, **a different picture
   while idle**.

That's the whole product for MVP. Everything else is "later."

The 480×480 canvas is a luxury vs. a strip display: full-screen pictures
look great and the tok/s number can be big. We do *not* spend the whole
resolution on it — the design stays "one big number + one small line."

---

## 2. Hardware (confirmed)

Board: **Waveshare ESP32-C6-Touch-AMOLED-2.16** (SKU 34201 = `-EN`, no
battery; 34202 = with LiPo header). Two variants: this project targets the
**`-EN` (wired, USB-C)** — battery is a "later" concern.

### 2.1 SoC & memory
- **ESP32-C6**: RISC-V, single core, 160 MHz. Wi-Fi 6 (802.11ax),
  BLE 5.3, Zigbee/Thread. No USB-serial-to-USB-C is a host port for data —
  USB-C is for flash + logs.
- **512 KB HP SRAM + 16 KB LP SRAM**, external **16 MB NOR flash**
  (partition: `app3M_fat9M_16MB` in the reference builds).

### 2.2 Display — the key fact (480×480, *not* 128×32)
- **480 × 480, 16.7M color, RGB565 framebuffer.**
- **Driver IC: product doc says `CO5300` over QSPI. The bundled Waveshare
  code ships and uses an `esp_lcd_sh8601` driver — a 4-bit *parallel*
  interface.** Treat the **bundled `sh8601` 4-bit driver as the source of
  truth** (it's what compiles and runs on the board); the CO5300/QSPI
  claim in the marketing sheet is unverified. *(Flagged discrepancy —
  verify on the real panel; do not hand-write a QSPI CO5300 driver on
  speculation.)*
- **Backlight: `GPIO_NUM_NC`** (AMOLED — no backlight to drive; brightness
  is set per-pixel / via panel command).
- Pinout (from `user_config.h` in the bundled Arduino + ESP-IDF examples):

  | Signal | GPIO |
  |---|---|
  | LCD CS | 15 |
  | LCD PCLK | 0 |
  | LCD DATA0–3 | 1, 2, 3, 4 |
  | (RST / DC / TE) | NC / via driver |

  **Conflict note:** the **SD card (TF slot) shares DATA0/DATA1/PCLK**
  (`SD_MOSI=1, SD_MISO=2, SD_CLK=0, SD_CS=6`) with the LCD. The reference
  design muxes these — **you can't have the TF card and the display
  simultaneously active on the same pins.** For MVP (bitmaps baked into
  flash) we ignore the TF slot. See §9 "image swapping" for the
  implications.

### 2.3 Touch (available, *optional* for MVP)
- **CST9220 capacitive touch** over I2C.
  - I2C bus: **SCL = 7, SDA = 8** (I2C_NUM_0).
  - `TOUCH_INT = 5`, `TOUCH_RST = 11`.
- MVP uses the **button** (§2.4) for the mode toggle. Touch is a *later*
  enhancement (e.g. tap-anywhere to toggle) — nice to have, not required.

### 2.4 Buttons
| Button | Role | Note |
|---|---|---|
| **KEY** | **The one user button for this project** — toggles Live ⇄ Screensaver. **GPIO NOT exposed in the bundled examples; confirm from the board schematic before wiring code.** | This is the input MVP depends on. |
| BOOT | Flash/download + "custom function" | Holds to enter download mode. **Do not use** for the toggle (it doubles as a download-mode pin and can fight the PCLK). |
| PWR | Power on/off | **Managed by the AXP2101 PMIC** (short = on, long = off). Not a plain GPIO — don't repurpose it. |

> The bundled Waveshare examples do **not** handle the KEY button, so its
> GPIO number is the one hardware fact still to be pinned down (read the
> schematic / measure, don't guess). Everything else above is confirmed.

### 2.5 Other on-board (unused in MVP, listed so we don't trip over them)
- **QMI8658** 6-axis IMU (I2C), **PCF85063** RTC (I2C), **AXP2101** PMIC
  (I2C, battery mgmt), **ES8311 + ES7210** audio codec + dual mics + speaker
  amp, **TF slot** (shares LCD pins, see 2.2). These coexist on the same
  I2C bus (SCL7/SDA8) — our firmware only touches I2C for touch, and can
  leave the rest untouched.

---

## 3. Network & server integration

> **Live probe:** the actual target server (`m.tower1:11444`) was probed on
> 2026-09-14 — both `/slots` and `/metrics` returned `200` (so `--metrics`
> is **on** and B10 is worth building). Exact field names + a re-verify
> script are in **`API_ENDPOINTS.md`**.

### 3.1 The localhost gotcha (read this first)

The server runs on **`localhost:11444`** *as seen from the host*. The
ESP32 **cannot** reach the host's `127.0.0.1`. It must talk to the
**host's LAN IP** (e.g. `192.168.x.y:11444`).

Consequences that must hold for the device to ever connect:

1. `llama-server` must bind to the LAN interface, **not** loopback only.
   If it currently binds `127.0.0.1`, start it with `--host 0.0.0.0` (or
   the specific LAN IP).
2. The device's config stores the **host IP + port** (default
   `127.0.0.1:11444` is a *wrong* default for the device — default it to
   whatever the LAN IP is, and make it editable).
3. Any host firewall must allow inbound on TCP `11444` from the device.

> This is the single most likely reason the device "won't connect." It
> must be documented on the setup page and surfaced as a distinct error
> state (§7).

### 3.2 Endpoints used

Primary — **`GET /slots`** (enabled **by default**, no extra server flag).
**Live-confirmed 2026-09-14** on `m.tower1:11444` (returned 200).

- Returns a JSON array of slots.
- Per slot the fields the device cares about:
  - `is_processing` (bool) — **the work/idle signal.**
  - `id` (int) — slot index.
  - `next_token[].n_decoded` (int) — tokens decoded **this slot**; a
    monotonically increasing counter usable to derive tok/s.
  - `next_token[].n_remain` (int) — remaining tokens for the current
    request (`-1` when unbounded). Useful context, not required.
- **Shape note (from the live probe):** `next_token` is an **array**, not a
  single object (the earlier draft described it as an object). When multiple
  entries exist, sum `n_decoded` across the array per slot and take the busy
  slot's `n_remain`. The `params` block is large and unused — parse
  defensively; any parse failure degrades to `OFFLINE_SERVER` (B9), never
  crashes. Full field list + `probe.sh` re-verify: **`API_ENDPOINTS.md`**.

Device derivation from `/slots` (single endpoint, no server flags needed):

- **working** = `any(slot.is_processing)` over all slots.
- **total decoded** = `sum(slot.next_token.n_decoded)`.
- **tok/s** = `Δ(total decoded) / Δ(t)` between consecutive polls, where
  `t` is wall-clock elapsed since the last successful poll (not the
  nominal poll interval — use real elapsed so slow polls don't skew the
  number).

Fallback / higher-precision — **`GET /metrics`** (only exists if the
server was started with `--metrics`). **Live-confirmed 2026-09-14:** this
server runs with `--metrics` on, so `/metrics` returned 200 and B10 is
worth building.

- `llamacpp:predicted_tokens_seconds` (gauge) — ready-made current
  generation throughput in tok/s.
- `llamacpp:requests_processing` (gauge) — alternate work/idle signal
  (0 = idle).
- `llamacpp:tokens_predicted_total` +
  `llamacpp:tokens_predicted_seconds_total` (counters) — an alternate,
  drift-free tok/s: `Δ(total tokens) / Δ(total seconds)` across two polls.
  Useful as a cross-check against the gauge, or a fallback if the gauge
  reads stale.

Decision: **implement `/slots`-based derivation as the primary path**
(works on any stock server). The `/metrics` gauge is a *nice-to-have*:
if the device ever learns `--metrics` is on (e.g. `/metrics` returns 200
vs 501), it may prefer the gauge for a smoother number. Do not *require*
it.

### 3.3 Polling cadence

| Mode | Poll rate | Why |
|---|---|---|
| Live, **working** | every **500 ms** | fast enough to make tok/s feel live; light on the box. |
| Live, **idle** | every **2 s** | no point hammering when nothing's generating. |
| Screensaver | every **2 s** | only need the work/idle state to pick the picture. |

- Use **HTTP GET** over the LAN (plain HTTP, no TLS in MVP — trusted local
  network; noted as a limitation).
- One request per poll; reuse a single `HTTPClient`/connection handle.
- Timeout per request: **1500 ms** (a LAN round trip is sub-ms; anything
  long means the server is gone or overloaded → drive the error state).

---

## 4. Data model (device-side)

A single struct drives the whole UI:

```
struct BuddyState {
    enum Conn   conn;      // OK | OFFLINE_SERVER | OFFLINE_WIFI | NO_SIGNAL
    bool        working;   // any slot is_processing
    float       toks_per_s; // derived (0 when idle)
    uint32_t    last_decode_total; // running counter for Δ
    int32_t     n_remain;   // next_token.n_remain of the busy slot (optional)
    enum Mode   mode;       // LIVE | SCREENSAVER
    uint32_t    last_poll_us; // wall clock of last successful poll
};
```

- `toks_per_s` is computed only while `working` **and** a successful
  previous poll exists. On the very first poll after a transition to
  working, show `—` for one cycle (no baseline yet) rather than a bogus
  spike.
- When `working` goes false, the displayed value decays to 0 (or shows
  `idle`) — do not freeze the last number.

---

## 5. Connection & signal model

| State | Meaning | Trigger |
|---|---|---|
| `OK` | LAN up + `/slots` answered 200 in time | successful poll |
| `OFFLINE_WIFI` | Wi-Fi down / not associated | no link, or ARP/DNS fails |
| `OFFLINE_SERVER` | Wi-Fi up but the server endpoint is unreachable | request timeout, TCP refused, HTTP 4xx/5xx, or JSON parse fail on `/slots` |
| `NO_SIGNAL` | reachable, server up, nothing generating — this is just *idle*, **not** an error | `is_processing` all false |

Distinguish **idle** (server fine, nothing generating) from **server
down** (endpoint unreachable). Idle is a healthy, boring state and shows
the idle screensaver picture; server-down shows an explicit error glyph
(§6.4). Do not conflate them.

Recovery: on `OFFLINE_*`, drop to a slow retry (every 5 s). On first
successful poll, snap back to `OK` immediately.

---

## 6. UI / screens

Canvas: **480×480, RGB565**. Use LVGL (v9, as Waveshare ships) so text,
widgets, and image decode are handled — do not hand-draw to a raw
framebuffer. Keep chrome minimal; the panel is the content.

### 6.1 Live mode — working
- Center, large font (~120 px): the number, e.g. `27.4`.
- Below it, small (~32 px): `tok/s`, and if `n_remain >= 0` a line
  `412 left`.
- Optional 1-cell "busy" dot or a thin progress arc while working.

### 6.2 Live mode — idle
- Center large: `—` (or `idle`).
- Below: `idle`.
- Dimmed / lower-contrast so the desk reads "asleep."

### 6.3 Screensaver mode (button toggles)
- **Working →** picture A (full 480×480).
- **Idle →** picture B (full 480×480).
- Images are 480×480 RGB565 (≈ 450 KB each). Ship two defaults baked into
  flash; make them user-swappable (see §9 image tool + storage caveat).
- Mode is independent of server state: the button changes which *kind* of
  thing is shown, the server state chooses the *variant*.

### 6.4 Error states (take over the display regardless of mode)
| State | Show |
|---|---|
| `OFFLINE_WIFI` | `wifi` icon + `no link` |
| `OFFLINE_SERVER` | `--` + `server down` |
- Error state wins over mode. A button press during an error does nothing.
  Recovers automatically.

### 6.5 Boot
- ~300 ms: "DeskBuddy" splash, then straight to Live mode.
- No pairing flow in MVP (Wi-Fi creds burned in at build / first flash,
  §8).

---

## 7. Behavior spec

| # | Behavior |
|---|---|
| B1 | On boot: connect Wi-Fi (stored creds), wait up to 10 s for link, then start polling. |
| B2 | Default mode on boot = **Live**. |
| B3 | **Single KEY-button press** toggles Live ⇄ Screensaver. Debounced ≥ 10 ms. Ignored while in an error state. *(Optional later: tap-anywhere on touch does the same.)* |
| B4 | Poll cadence adapts: 500 ms while working (Live), 2 s otherwise (§3.3). |
| B5 | tok/s = Δ(decoded total) / real elapsed between polls, computed only while working. First working cycle shows `—`. |
| B6 | `working` = any slot `is_processing`. Idle is a normal state, not an error. |
| B7 | Endpoint unreachable → `OFFLINE_SERVER`; Wi-Fi down → `OFFLINE_WIFI`; both override the screen and throttle to 5 s retries. |
| B8 | On returning to `OK`, refresh immediately and restore the pre-error mode. |
| B9 | If `/slots` returns a parse error (schema drift), treat as `OFFLINE_SERVER` and keep retrying — do not crash. |
| B10 | If the server was started with `--metrics` and `/metrics` returns 200, the device *may* prefer the `predicted_tokens_seconds` gauge for a smoother tok/s (optional, non-blocking). |

---

## 8. Configuration

Stored in NVS so it survives reboots and can be changed without reflashing.

| Key | Default | Notes |
|---|---|---|
| `wifi.ssid` | (burned in at first flash) | |
| `wifi.pswd` | (burned in at first flash) | |
| `llama.host` | `m.tower1` (or its LAN IP) | **LAN IP/hostname, not 127.0.0.1.** Probe-confirmed target. |
| `llama.port` | `11444` | |
| `poll_ms_working` | `500` | |
| `poll_ms_idle` | `2000` | |
| `screensaver.imgA` | built-in default | picture for "working" |
| `screensaver.imgB` | built-in default | picture for "idle" |

- First-flash provisioning: a small `tools/provision` helper (or a serial
  menu) sets SSID/PSWD/host. No Wi-Fi picker UI on the panel in MVP.
- All numeric knobs have sane bounds (poll interval clamped 200 ms–10 s).

---

## 9. Non-functional requirements

| Area | Requirement |
|---|---|
| Latency | Live number tracks generation within one poll interval (≤ 500 ms when working). |
| Robustness | Never crash on malformed/missing JSON, network blips, or schema drift — degrade to an error state and retry. |
| Display | LVGL v9 on the 480×480 panel; render on change only (no full-panel refreshes every frame). |
| Image storage | 480×480 RGB565 ≈ 450 KB/picture. Two default images fit easily in the 3 MB app partition. **Swapping without reflash is constrained by the LCD/SD pin-mux** (§2.2) — MVP ships baked-in images; live-swap is post-MVP and needs the pin-conflict resolved. |
| Firmware | Arduino (arduino-esp32 ≥ 3.3.0) **or** ESP-IDF (v5.5.x) build. C++ is fine; keep the HTTP + JSON path tiny and dependency-light. Start from Waveshare's bundled `09_LVGL_V9_Test` Arduino example (it already initializes display + touch correctly). |
| Build/flash | One command flashes the board. `platformio.ini` **or** an Arduino `.ino` project **or** ESP-IDF at repo root — pick one, document the FQBN/partition (`esp32c6, FlashSize=16M, app3M_fat9M_16MB`). |
| Testability | The state machine (`BuddyState` transitions, tok/s math, error handling) must be unit-testable **without** hardware — pure logic, I/O injected. This is the part worth testing. |
| Security | Plain HTTP on a trusted LAN in MVP. No secrets transmitted by the device other than the (LAN-local) Wi-Fi creds. Acceptable; documented. |

---

## 10. Repo layout (proposed)

```
deskbuddy/
├── PRODUCT_SPEC.md          # this file
├── README.md                # setup: bind 0.0.0.0, set LAN IP, flash, FQBN
├── firmware/                # Arduino or PlatformIO project (start from Waveshare 09_LVGL_V9_Test)
│   ├── src/
│   │   ├── buddy_state.c    # state machine + tok/s math (unit-testable)
│   │   ├── poller.c         # HTTP GET /slots (+ optional /metrics)
│   │   ├── ui.c             # LVGL Live / screensaver / error screens
│   │   ├── button.c         # KEY debounce + press detect
│   │   ├── display_bsp.c    # wraps Waveshare sh8601 init (4-bit, pins §2.2)
│   │   └── config.c         # NVS load/save
│   └── assets/              # default 480×480 RGB565 bitmaps (imgA, imgB)
├── tools/
│   ├── provision/           # set Wi-Fi creds + host (serial or helper)
│   └── img2buddy/           # convert a 480×480 PNG → RGB565 .bin for the panel
└── tests/                   # host-side unit tests for buddy_state.c
```

---

## 11. Milestones

**M1 — Skeleton (proves connectivity)**
- Reuse Waveshare's LVGL bring-up to get a panel on, then add a Wi-Fi +
  `/slots` poller that logs `working/tok-s` to serial.
- *Exit:* serial shows live tok/s while a chat generates, `idle` when it
  stops. (Also proves the `0.0.0.0` binding works.)

**M2 — Live screen**
- Render tok/s + idle on the 480×480 AMOLED. Error states for server/wifi
  down.
- *Exit:* the desk display shows a correct, live number and degrades
  gracefully when the server is killed.

**M3 — Screensaver + button**
- KEY button toggles Live ⇄ Screensaver; screensaver shows imgA (working)
  / imgB (idle). `img2buddy` tool ships two default 480×480 images.
- *Exit:* full MVP behavior as described in §1.

**M4 — Polish (post-MVP)**
- Optional `/metrics`-gauge path for a smoother number (B10).
- Touch (CST9220) tap-anywhere as an alternate toggle.
- Image swapping without reflash (resolve the LCD/SD pin-mux first, §2.2).
- RTC-driven clock + "time since last request" on idle.
- Battery/low-power idle (AMOLED blanking) if ever run on the LiPo version.

---

## 12. Out of scope (explicitly, for now)

- Battery operation (target the wired `-EN` variant).
- Multi-server / router-mode aggregation (`?model=` handling).
- TLS/mTLS to the server.
- Voice, sound, IMU, or any non-display feedback (audio/IMU/RTC are on the
  board but unused in MVP).
- A Wi-Fi configuration UI on the panel itself.
- Prompt-processing speed (only *generation* tok/s is shown in MVP).

---

## 13. Open questions (need answers before M2)

1. **KEY button GPIO** — the one unconfirmed pin. Read it off the
   schematic (or `dmesg`/measure on the board). Everything else in §2 is
   confirmed from the bundled code.
2. **Display driver** — confirm the panel actually wants the bundled
   `sh8601` 4-bit driver (trust the code) vs. the doc's `CO5300`/QSPI.
   If the bring-up example works unmodified, we're using the right driver.
3. **Is `--metrics` currently on** the user's server? → **Resolved
   2026-09-14: yes.** `GET /metrics` returned 200 on `m.tower1:11444`, so
   B10 (gauge path) is worth building. See `API_ENDPOINTS.md`.
4. **Framework** — Arduino (gentler, big LVGL v9 example already present)
   vs. ESP-IDF (finer control). Recommend **Arduino** to move fastest;
   the reference examples make display+touch nearly free.
5. **Screensaver images** — any preference for the two default pictures,
   or should I pick/generate placeholders?
