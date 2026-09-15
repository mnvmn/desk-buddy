# DeskBuddy Windows Widget — Spec

A **sibling** of `PRODUCT_SPEC.md` (the ESP32 AMOLED device). Same data
source, same tok/s math, same parsing rules — but rendered as a **floating
card on the Windows 11 desktop** instead of the panel. Read
`API_ENDPOINTS.md` for the live-confirmed endpoint/field details; this spec
only adds the *Windows UI + process* layer.

Status: draft v0.1 · Owner: — · Last updated: 2026-09-14

---

## 1. Goal

A small, always-on-top, drag-anywhere card on the desktop that answers the
same question as the device at a glance: **is my model generating right now,
and how fast?**

It shows:
1. A large **tok/s** number, live while generating.
2. **Idle** (dimmed) when the server is up but nothing is generating.
3. **Server down** (error accent) when the endpoint is unreachable.

That's the whole widget for MVP. Same scoping philosophy as the device:
do the two things well, everything else is "later."

**Reuse, don't re-derive:** the network + state logic is the same
`BuddyState` model from the device spec (§4) and the derivation rules from
§3.2 / `API_ENDPOINTS.md`. The only net-new code is the window, the render,
the interaction, and config persistence.

---

## 2. Framework decision

**Python 3.11 + PySide6 (Qt 6).** Rationale:

- Python 3.11.16 is already present on this host — no new toolchain.
- PySide6 gives first-class **frameless + always-on-top + translucent +
  borderless** windows, which is exactly a floating desktop widget.
- `QNetworkAccessManager` performs HTTP **on the Qt event loop** — no manual
  worker threads, no GIL/Qt cross-thread signal gymnastics for the poller.
- QSS (Qt style sheets) style the rounded translucent card with a few lines.
- Single-file-ish app; trivial to run: `python widget.py`.

Alternatives considered:
- **`tkinter`** — zero deps, but transparent/rounded/DPI windows are awkward
  and it looks dated on Win11. Rejected.
- **Rust + egui** — the *leanest* always-on option (one small native binary,
  near-zero RAM). The right call **if/when** you want a self-contained
  `.exe` with no Python. Kept as the migration path, not the starting point.
- **C# WPF** — native + real Mica/Acrylic, but heavier toolchain; overkill.

> **Dependency note:** PySide6 must be installed (`pip` is not currently on
> PATH). Set up a dedicated venv (or use `uv`) for the widget so the install
> is isolated and reproducible. See §9 setup.

---

## 3. Data model & parsing (reused)

Same struct, same rules as the device — copy the semantics, not the file:

```
BuddyState {
    conn    : OK | SERVER_DOWN          # desktop drops OFFLINE_WIFI (no Wi-Fi to lose)
    working : bool                       # any slot.is_processing
    toks    : float                      # 0 when idle
    ...
}
```

- **`working`** = `any(slot.is_processing)` over `GET /slots`.
- **tok/s** — primary: `Δ sum(next_token[].n_decoded) / Δ real elapsed`
  between consecutive polls (use *real* elapsed, not the nominal interval).
  First working cycle shows `—` (no baseline).
- **tok/s** — preferred when available: `llamacpp:predicted_tokens_seconds`
  gauge from `GET /metrics` (this server has `--metrics` on, so it returns
  200). Use the gauge for a smoother number; fall back to the derived value
  if `/metrics` is 404/501 or the gauge is stale/absent.
- Parse **defensively**: `next_token` is an **array**; on any JSON/schema
  failure degrade to `SERVER_DOWN` and keep retrying — never crash.

### Connection states (desktop-adapted)

| State | Meaning | Trigger |
|---|---|---|
| `OK` (working) | endpoint answered 200, generating | `any is_processing` |
| `IDLE` | endpoint up, nothing generating — **healthy**, not an error | all `is_processing` false |
| `SERVER_DOWN` | endpoint unreachable | timeout, TCP refused, 4xx/5xx, or parse fail |

`IDLE` is a normal, boring state (show the dimmed card). Do **not** conflate
it with `SERVER_DOWN` (show the error accent). On `SERVER_DOWN`, drop to a
slow retry (every 5 s); on first 200, snap back immediately and restore the
previous layout mode.

---

## 4. Polling

- **Driver:** a `QTimer` drives polls; each poll is a `QNetworkAccessManager`
  `GET`. The adapter interval updates per poll (no busy loop, no threads).
- **Cadence** (mirror the device):

  | State | Interval | Why |
  |---|---|---|
  | Working | **500 ms** | live feel |
  | Idle | **2 s** | nothing to watch |
  | Server down | **5 s** | stop hammering a dead endpoint |

- **One request per poll**; reuse a single `QNetworkAccessManager` /
  connection. Per-request timeout **1500 ms** (a LAN round trip is sub-ms;
  anything long → `SERVER_DOWN`).
- Plain HTTP, trusted LAN (same security posture as the device — documented
  limitation). No TLS in MVP.

---

## 5. The window (Win11 floating card)

A borderless top-level `QWidget`:

- `Qt.FramelessWindowHint` — no title bar.
- `Qt.WindowStaysOnTopHint` — always on top.
- `Qt.Tool` — keeps it off the taskbar and out of Alt-Tab.
- **Translucent** background with a **rounded card**:
  - default card ≈ **260 × 120 px**, corner radius 14 px,
    background `rgba(30, 30, 34, 200)`, 1 px border `rgba(255,255,255,28)`.
  - **Compact mode** ≈ **96 × 96 px**, radius 16 px, number only.
- **HiDPI:** enable `Qt.AA_EnableHighDpiScaling`; design in logical px so it
  stays crisp on 125/150 % displays.
- **Skip the taskbar** (`Qt.Tool`) and **stay put** across re-renders.
- Layout is render-on-change only (update the label on each poll; don't
  repaint the whole card every frame).

### Content

| Element | Working | Idle | Server down |
|---|---|---|---|
| Big number (~56 px, accent) | `27.4` | `—` | `--` |
| Sub-label (~14 px, muted) | `tok/s` | `idle` | `server down` |
| Status dot | ● green | ● gray | ● red |
| Card opacity | 1.0 | ~0.6 (dimmed) | 1.0, red accent |

- Optional (later): a thin **sparkline** of the last N tok/s samples under
  the number.
- Optional (later): a `N left` line from `n_remain` when `>= 0`.

---

## 6. Interaction

- **Click + drag** anywhere on the card to move it (custom
  `mousePressEvent`/`mouseMoveEvent` → `move()`).
- **Double-click**: toggle **Full ⇄ Compact**.
- **Right-click** context menu:
  - Full / Compact
  - Pin on top (on/off)
  - Hide (to tray)
  - Quit
- **Tray icon**: click to show/hide the card; keep the poller running while
  hidden.
- **Position persisted** in config; restored on next launch.

---

## 7. Configuration

JSON file at **`%APPDATA%/DeskBuddy/widget.json`** (fallback: app dir).
Survives restarts; editable without rebuild.

| Key | Default | Notes |
|---|---|---|
| `host` | `m.tower1` | **LAN host/hostname, not 127.0.0.1** (probe-confirmed). |
| `port` | `11444` | |
| `poll_working_ms` | `500` | clamp 200–10 000 |
| `poll_idle_ms` | `2000` | clamp 200–10 000 |
| `poll_error_ms` | `5000` | |
| `size_mode` | `full` | `full` \| `compact` |
| `pos` | `{x,y}` | last saved card top-left |
| `opacity` | `1.0` | card alpha |
| `accent` | `#4ec9b0` | working number / dot color |
| `sparkline` | `false` | later |

Missing keys → defaults. Malformed JSON → ignore file, use defaults, log a
warning (never crash).

---

## 8. Non-functional requirements

| Area | Requirement |
|---|---|
| Latency | Number tracks generation within one poll interval (≤ 500 ms working). |
| Robustness | Never crash on malformed/missing JSON, network blips, or schema drift — degrade to `SERVER_DOWN` and retry. |
| Footprint | Low: near-idle CPU at the 2 s idle cadence; target < ~50 MB RAM resident. |
| Single instance | A named mutex (or lockfile) so launching a second copy no-ops — no duplicate cards. |
| HiDPI | Correct at 100/125/150 % — logical-pixel layout, crisp text. |
| Unattended | Runs quietly: no taskbar entry, no focus stealing, survives being dragged to any monitor. |
| Logging | Quiet by default; `-v` / `--log` flag prints state transitions to console (for M1). |

---

## 9. Repo layout & setup

```
desk-buddy/
├── PRODUCT_SPEC.md            # device (ESP32)
├── API_ENDPOINTS.md           # live endpoint/field reference (shared)
├── WINDOWS_WIDGET_SPEC.md     # this file
└── widget/                    # PySide6 app
    ├── pyproject.toml         # or requirements.txt: PySide6
    ├── widget.py              # entry: window + event loop
    ├── buddy_state.py         # state model + tok/s math (shared logic, importable)
    ├── poller.py              # QNAM /slots (+ /metrics) adaptive polling
    ├── render.py              # QSS card, full/compact, status states
    └── config.py              # JSON load/save (%APPDATA%)
```

**Setup (one-time):**
```
cd widget
python -m venv .venv
.venv/Scripts/activate
python -m pip install --upgrade pip
python -m pip install PySide6
python widget.py
```
(If `pip` is missing: `python -m ensurepip --upgrade` first, or use `uv`.)

**Run:** `python widget.py` → floating card appears; verify it shows the
live number against `m.tower1:11444`.

---

## 10. Milestones

**M1 — Polling proves connectivity**
- `buddy_state.py` + `poller.py` against `m.tower1:11444`, log
  `working / toks / state` to console. No UI yet.
- *Exit:* console shows a live tok/s while a request generates, `IDLE` when
  it stops, `SERVER_DOWN` when the server is killed.

**M2 — The floating card**
- Render the rounded always-on-top translucent card: big number, `tok/s`,
  status dot; idle-dim and server-down accent. Adaptive polling.
- *Exit:* the desktop shows a correct, live number and degrades gracefully
  when the server is stopped.

**M3 — Interaction + persistence**
- Drag, double-click full/compact, right-click menu, tray show/hide, config
  persistence + restore, single-instance guard.
- *Exit:* full MVP widget behavior as described in §1.

**M4 — Polish (post-MVP)**
- Sparkline of recent tok/s.
- Mica/Acrylic background (via the `qfluentwidgets` library or a compositing
  hook) for a native Win11 look.
- Per-monitor always-on-top + auto-start at Windows login.
- Optional single-`.exe` packaging (`PyInstaller`) **or** migration to
  Rust + egui if a lean native binary is wanted.

---

## 11. Out of scope (for now)

- Click-through / transparent-to-mouse modes.
- Multiple servers / aggregation.
- TLS to the server.
- Any non-visual feedback (sound, toast).
- Full Win11 Mica theme (post-MVP; M4).

---

## 12. Open questions

1. **Python route confirmed?** Given `pip` is currently missing, is a
   dedicated venv (or `uv`) fine, or do you want to skip to Rust + egui for a
   self-contained `.exe`?
2. **Default look** — dark compact pill (96×96) or the 260×120 card? And a
   default corner (top-right is typical)?
3. **Accent color** — any preference, or keep `#4ec9b0`?
4. **Auto-start at login** — do you want it in the Startup folder /
   Task Scheduler, or launch it manually?
