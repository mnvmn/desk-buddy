# DeskBuddy Windows Widget — Spec

A **sibling** of `PRODUCT_SPEC.md` (the ESP32 AMOLED device). Same data
data source, same tok/s math, same parsing rules — but rendered as a
**floating card on the Windows 11 desktop** instead of the panel. Read
[../shared/API_ENDPOINTS.md](../shared/API_ENDPOINTS.md) for the
live-confirmed endpoint/field details; this spec only adds the *Windows UI
+ process* layer.

Status: draft v0.3 · Owner: — · Last updated: 2026-09-20 (M3: multi-server vertical card + config file)

---

## 1. Goal

A small, always-on-top, drag-anywhere card on the desktop that answers the
same question as the device at a glance — **for each inference server**:
**is my model generating right now, and how fast?**

The card scales **vertically**: it stacks **one row per configured server**
(top = first), and its window height grows with the server count. For now the
config lists the same host **twice** (two rows, `tower1`/`mini1`); adding a real
second server is an edit to the config file, not a recompile.

Each server row shows:
1. A large **tok/s** number, live while generating.
2. **Prompting** (amber) when a request is in flight but tokens haven't
   started yet.
3. **Idle** (dimmed) when the server is up but nothing is generating.
4. **Server down** (error accent) when the endpoint is unreachable.

When their data is available (best-effort) each row also shows a **model + ctx**
line, a **~N s left · N tok** leftover, and a **session footnote** (avg tok/s,
KV-cache reuse). Same scoping philosophy as the device: the number + states
are the point; the rest only appears when the server provides it.

The **scene mode** shows **combined activity** — one animated level band per
server, stacked — so the whole fleet's load is visible at a glance.

**Reuse, don't re-derive:** the network + state logic is the same
`BuddyState` model from the device spec (§4) and the derivation rules from
§3.2 / `API_ENDPOINTS.md`. The only net-new code is the window, the render,
the interaction, and the config file (servers + cadence).

---

## 2. Framework decision

**Chosen: Rust + eframe/egui** (PoC implemented 2026-09-19). Rationale:

- egui's **native windows are already "typical Windows UI"** — title bar,
  close/min/max buttons, taskbar entry — for free. No compositing work.
- **One self-contained `.exe`**, near-zero dependencies at runtime, low RAM
  for an always-on process — the exact reason this project is a widget.
- `reqwest`/`ureq` + a background thread (or `smol`/`tokio`) drive the poll;
  state is handed back to the UI frame.
- Toolchain on this host is **no-admin**: Rust **GNU** target
  (`stable-x86_64-pc-windows-gnu`) + portable **WinLibs MinGW-w64 (UCRT)**.
  See §13 for the exact setup (no Visual Studio / MSVC available).

The earlier draft leaned Python/PySide6 for dev speed; we switched to Rust
because the *packaging* win (standalone `.exe`, low footprint) outweighs the
small extra setup cost for a project this small. `buddy_state` / tok/s math
ports 1:1 (pure logic, injected I/O).

Alternatives considered:
- **Python + PySide6** — fastest to a styled card, but ships a Python runtime;
  rejected in favor of a self-contained binary.
- **C# WPF** — native + Mica/Acrylic, but heavier toolchain; overkill.

---

## 3. Data model & parsing (reused)

Same per-server struct, same rules as the device — copy the semantics, not the
file. The UI reads a **`WidgetState`** that holds **one `BuddyState` per
configured server** (plus labels + the shared display mode):

```
WidgetState {
    servers    : Vec<BuddyState>        # index 0 = top row
    labels     : Vec<String>            # per-server short name (footer)
    display_mode: Data | Scene          # shared across the whole card
}
BuddyState {
    conn    : OK | SERVER_DOWN          # desktop drops OFFLINE_WIFI (no Wi-Fi to lose)
    working : bool                       # any slot.is_processing
    toks    : float                      # 0 when idle
    host_label: String                   # the host string polled (dim footer)
    ...
}
```

- **`working`** = `any(slot.is_processing)` over `GET /slots`.
- **tok/s** — primary and only **display** path: `Δ next_token[].n_decoded / Δ real elapsed`
  between consecutive polls, using **only the processing slot's** `n_decoded`
  (parity P2 — NOT a sum over all slots; an idle slot's stale counter
  double-counts when >1 slot runs in parallel and poisons the next request's
  baseline, reading the rate as 0). Use *real* elapsed, not the nominal
  interval. First working cycle shows `—` (no baseline). The `/metrics`
  `predicted_tokens_seconds` gauge is a **lagging session-average** (verified
  to read `0.0` during generation) — it is shown only as a **footnote**, never
  the headline number.
- **Prompting** = `working` true but `n_decoded == 0` (the request is in
  flight but the prompt is still being preprocessed). A sub-state of the
  working card; derived from `/slots` alone.
- **Prefill progress** — headline % during `Prompting`:
  `n_prompt_tokens_processed / n_prompt_tokens` from the `/slots` hot poll
  (a 0→1 ratio as the prompt is processed).
- **Prefill tok/s** — `Δ n_prompt_tokens_processed / Δ real elapsed` between
  consecutive working polls, same philosophy as the decode tok/s (a fresh
  baseline per working cycle, so the rate resets when a new prompt starts).
  Shown only while `Prompting` (decode tok/s owns the headline in
  `Generating`).
- **Parallel slots** — `len(/slots)`, the number of concurrent requests the
  server accepts. Shown as a leading `×N slots` chip on the **capacity line**
  (`×2 slots · 72k/160k · 45% · Q4_K - Medium`). A non-zero count only (a
  transient empty response can't blank the chip).
- **Model / ctx** from `GET /props`, fetched **once at startup** and re-fetched
  every **~60 s** (best-effort) so a model swap is picked up without a restart.
- **Session stats** (avg tok/s footnote, KV-cache reuse, speculative-decode
  acceptance) from `GET /metrics`, fetched on a slow ~2 s background pass
  (best-effort). Spec-acceptance = `spec_decode_num_accepted_tokens_total /
  spec_decode_num_draft_tokens_total` (cumulative ratio → `spec NN%` chip).
- **Session token total** — `prompt_tokens_total + tokens_predicted_total`
  from `/metrics` (cumulative counters, reset on server restart). Shown as a
  right-aligned `NNk total` on the dim per-server **footer** (not the footnote
  row, which stays short so the tok/s sparkline has room). It is a per-server
  *session* total, not all-time. `prompt_tokens_cached_total` is deliberately
  **not** used (it counts KV-cache reuses, so it's inflated, not a true
  total).
- Parse **defensively**: `next_token` is an **array**; on any JSON/schema
  failure degrade to `SERVER_DOWN` and keep retrying — never crash. Only
  `/slots` failures affect `conn`; `/props`/`/metrics` failures just leave
  those fields empty.

### States (desktop-adapted)

The card renders a **phase** derived from `(conn, working, n_decoded)`:

| Phase | Meaning | Trigger |
|---|---|---|
| `Generating` | endpoint up, tokens streaming | `any is_processing` **and** `n_decoded > 0` |
| `Prompting` | request in flight, prompt still processing | `any is_processing` **and** `n_decoded == 0` |
| `Idle` | endpoint up, nothing generating — **healthy**, not an error | all `is_processing` false |
| `Down` | endpoint unreachable | timeout, TCP refused, 4xx/5xx, or parse fail |

`Idle` is a normal, boring state (show the dimmed card). Do **not** conflate
it with `Down` (show the error accent). On `Down`, drop to a slow retry (every
5 s); on first 200, snap back immediately and restore the previous layout mode.
`Prompting` is amber, headline = the **prefill %** (`42%`) with sub-label
`prompting · 2.0k tok/s` (prefill rate), so the user sees the prompt being
processed and how fast — distinct from the green decode number in
`Generating`. Before the first prefill figure it falls back to `···` /
`prompting…`.

---

## 4. Polling

- **Driver:** a single background OS **thread** polls **every configured
  server** (the server list comes from the config file, §7); each server gets
  its own `GET`. The thread never holds the state lock while doing network
  I/O — it fetches all servers first, then applies the batch under one short
  lock, so a slow/unreachable server (1.5 s timeout) never stalls the UI.
- **Cadence** (values come from the config file; defaults below, mirror the
  device):

  | State | Default interval | Why |
  |---|---|---|
  | Working | **500 ms** | live feel |
  | Idle | **2 s** | nothing to watch |
  | Server down | **5 s** | stop hammering a dead endpoint |

  The thread's sleep is the **minimum** across all servers, so a busy server
  tracks fast even if another is idle. Each `BuddyState` keeps its own
  phase-driven cadence (`poll_interval(tuning)`).
- **One request per server per poll**; per-request timeout **1500 ms** (a LAN
  round trip is sub-ms; anything long → `SERVER_DOWN`).
- **Secondary endpoints are off the hot path**: `GET /props` is fetched
  **once at startup** per server (model / quant / ctx) and then re-fetched
  every **~60 s** so a model swap is picked up without a restart; `GET /metrics`
  runs on a slow **~2 s** background pass per server (session stats). Both use a
  short 1000 ms timeout and are best-effort — a failure leaves those fields
  empty; only a `/slots` failure changes that server's `conn`.
- Plain HTTP, trusted LAN (same security posture as the device — documented
  limitation). No TLS in MVP.

---

## 5. The window (Win11 floating card)

A borderless top-level **egui/eframe** window:

- `with_decorations(false)` — no title bar.
- `with_always_on_top()` — always on top.
- `.with_transparent(true)` + a **rounded card** painted by the app:
  - **320 px wide** (fixed); **height scales with the server count**:
    `HEADER_H (34) + n·ROW_H (176) + (n−1)·ROW_GAP (10) + FOOT_PAD (14)`.
    Two servers → 320 × 410; `card_height(n)` computes it from the config.
  - corner radius 14 px.
- **HiDPI:** design in logical px so it stays crisp on 125/150 % displays
  (goldens pin `pixels_per_point = 2.0`).
- **Skip the taskbar** and **stay put** across re-renders.
- Layout is render-on-change only (update each row on each poll; the scene
  animates at ~8 fps).

### Content (data mode)

**One row per server**, stacked top→bottom in config order, separated by a
thin rule. Each row has the same four-line structure, chosen by that
server's `BuddyState::phase()`:

| Element | Generating | Prompting | Idle | Down |
|---|---|---|---|---|
| Status dot | ● green | ● amber | ● gray | ● red |
| Big number (~40 px, accent/amber) | `127.4` | `42%` (prefill progress) | `—` (dimmed) | `--` |
| Sub-label (~13 px, muted) | `tok/s` | `prompting · 2.0k tok/s` (prefill rate) | `idle` | `server down` |
| Model + capacity (right, muted) | `Qwen3.8-27B-UD-Q4_K_XL` / `×2 slots · 72k/160k · 45% · Q4_K - Medium` | same | same | `--` |
| Leftover (only when `n_remain > 0`) | `~42s left · 1,847 tok` | `processing prompt` | `loaded · ready` | `retrying in 5 s` |
| Sparkline + session footnote | ▂▃▅▇ bars + `avg 121 · 92% cached · spec 71%` | — (footnote `avg … · 92% cached · spec 71%` once available) | — | — (hidden) |
| Footer (always, dim) | `tower1 · m.tower1:11444` (left) + `64.7k total` (right) | same | same | same |

- The model line is **hidden** until `/props` resolves (best-effort); the
  leftover line is **hidden** when `n_remain < 0` (unbounded request); the
  sparkline + session footnote are **hidden** until their data arrives — and
  hidden entirely in the `Down` state. The capacity line leads with the
  **parallel slot count** (`/slots` array length) as a `×N slots` chip, then
  live context occupancy (`n_prompt_tokens / n_ctx`, from the `/slots` hot
  poll) as `72k/160k · 45%` before the quant string, turning **amber** past
  **80%** usage; before the first occupancy figure it falls back to
  `×2 slots · 160k ctx · Q4_K - Medium`.
- The model line **shrinks its font** (12 → 11 → 10 px, `fit_font`) before
  ever chopping the name, so the model keeps its identity; `fit_text` adds a
  front-first ellipsis only as a last resort if even the smallest size
  overflows. Its right edge is anchored to `close.left() − 8 px` so a long
  live name never runs under the corner chrome.

### Display mode 2: Scene ("Plumber's run", approved 2026-09-20)

The card can swap the data layout for **animated pixel scene** — showing
**combined activity**: one full day-level **band per server**, stacked
top→bottom in config order, each in that server's own phase. Design sheet:
`shared/scene-assets/scene_design_sheet.png`; implementation: `src/main.rs`
blits the generated `scene_assets` bitmaps from
`shared/scene-assets/` (80×48 grid pre-rendered per state — no mesh, no
live animation).

| Phase (per band) | Scene |
|---|---|
| Generating | **Day** — the red-cap plumber (original character, classic palette — no Nintendo sprites) sprints **on the ground**; the ground scrolls and the far cloud sweeps the sky. No coins. |
| Prompting | **Same day scene as Generating** (the picture is reused — no distinct art). |
| Idle | **Day** — the same bright level, *calmed*: props frozen, **no plumber** (scene only). |
| Down | Idle day scene + a **Piranha Plant** roaring up in the middle of the scene (red spotted head, open fanged maw, green stalk). No plumber, no coins. |

- Each band is a **scaled copy of the same 80×48 level** (cell size derived
  from the band rect), so the fleet reads as one coherent world. Beside each
  band: a **status dot** (green/amber/gray/red by phase) + the server's
  **label** (`tower1`, `mini1`, …).
- Animation: ~8 fps loop (125 ms steps from the wall clock; fixed frame in
  `EFRAME_SCREENSHOT_TO` mode for stable goldens). The scene repeats exactly
  every `LOOP = 960` frames (LCM of the 8-frame character cycle and the
  slower event sub-loops — scroll, blink, cloud sweep). The two
  tempos differ only in which elements move: see
  `wiki/widget/modules/scene-design.md`.
- **Click the card content** (anywhere below the drag strip, off the close
  button) to flip the whole card between data card and scene — there is no
  dedicated mode button. A real click only: egui's `primary_clicked()`
  discards drags, so moving the window by the strip never toggles. The
  close ×' right-click menu still offers *Switch to scene / Switch to data
  card / Quit*. The mode is a single `WidgetState::display_mode` — one
  click flips the whole card.

---

## 6. Interaction

- **Drag strip (the "drag-and-drop pane"):** a band across the **top** of the
  card (height 22 px, with a 3-line **grip** glyph at the top-left).
  Press + drag anywhere in it (outside the corner buttons) moves the window
  via `ViewportCommand::StartDrag` (winit `drag_window()` — the native
  Windows move primitive; the grab point stays fixed under the cursor).
  Shared by both display modes (`draw_chrome`).
- **Top-right chrome** (the visible window controls, since the card is
  borderless):
  - **Close ×** — `ViewportCommand::Close`.
  - **Right-click** on it: *Switch to data card / Switch to scene / Quit*.
  - The chip is opaque (fill `#30343c`, border `#969ba8`) so it reads
    as a control on the dark card; hover lightens it.
- **Mode toggle** is **not** a button — a **click on the card content**
  flips data ⇄ scene (see §5.1).
- **Data-card model line** shrinks its font (`fit_font`, 12 → 11 → 10 px)
  before ever ellipsizing, so the model keeps its identity, and its right edge
  is anchored to `close.left() − 8 px` so a long live model name can never run
  under the corner chrome.
- **Double-click**: toggle **Full ⇄ Compact** (still M3).
- **Tray icon** (still M3): click to show/hide the card; keep the poller
  running while hidden.
- **Position persisted** in config; restored on next launch (still M3).

---

## 7. Configuration

JSON file at **`%APPDATA%/DeskBuddy/widget.json`** (override the path with
`DESKBUDDY_CONFIG`). Written on first run (so there's a file to edit); editable
without rebuild. **This is where the server list and the poll cadence live** —
adding a server is an edit, not a recompile.

```json
{
  "servers": [
    { "host": "m.tower1:11444", "label": "tower1" },
    { "host": "m.mini1:11444", "label": "mini1" }
  ],
  "poll": { "working_ms": 500, "idle_ms": 2000, "error_ms": 5000 }
}
```

| Key | Default | Notes |
|---|---|---|
| `servers[]` | `m.tower1:11444` (tower1) + `m.mini1:11444` (mini1) | **the server list, in card order** (index 0 = top row). Each is `host` (required, `host:port` — LAN, not 127.0.0.1) + optional `label` (defaults to `host`). |
| `poll.working_ms` | `500` | clamp 200–10 000 |
| `poll.idle_ms` | `2000` | clamp 200–10 000 |
| `poll.error_ms` | `5000` | clamp 200–10 000 |

Missing keys → defaults. An empty/blank server list → the 2-server default.
Malformed JSON → ignore the file, use defaults, log a warning (never crash).

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
├── README.md                # project overview + map
├── shared/
│   ├── API_ENDPOINTS.md     # live endpoint/field reference (shared)
│   └── tools/probe.sh       # one-command server re-verify
├── esp32c6_480x480/
│   └── PRODUCT_SPEC.md      # device (ESP32) spec
├── widget/
│   ├── WINDOWS_WIDGET_SPEC.md     # this file
│   └── widget/                # PySide6 app
│       ├── pyproject.toml         # or requirements.txt: PySide6
│       ├── widget.py              # entry: window + event loop
│       ├── buddy_state.py         # state model + tok/s math (shared logic, importable)
│       ├── poller.py              # QNAM /slots (+ /metrics) adaptive polling
│       ├── render.py              # QSS card, full/compact, status states
│       └── config.py              # JSON load/save (%APPDATA%)
└── wiki/                    # compiled reference docs
```

**Setup (one-time):**
```
cd widget/widget
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

**M1 — Polling proves connectivity — DONE (2026-09-20)**
- `buddy_state.rs` + `poller.rs` (Rust) against `m.tower1:11444`; the poller
  logs `host / conn_ok / working / toks` per poll to `widget_state.log`. The
  tok/s math is unit-tested (`cargo test`, 7 tests incl. a live `fetch_slots`).
- Verified end-to-end against `tools/mock_server.py cycle`: 100 polls showed
  correct busy↔idle transitions, ~95 tok/s, and adaptive cadence
  (0.50 s busy / 2.00 s idle). See §14 for how to reproduce.
- *Exit (met):* a live tok/s while generating, `IDLE` when it stops,
  `SERVER_DOWN` when the server is unreachable.

**M2 — The data-driven card — DONE (2026-09-20)**
- `BuddyState` now models a **phase** (`Generating` / `Prompting` / `Idle` /
  `Down`) and the card renders it: live tok/s, the new amber **prompting**
  state (`is_processing` + `n_decoded == 0`), idle-dim, and the error accent.
- New best-effort data: **model + quant + ctx** line from `/props` (once at
  startup), **~N s left · N tok** leftover from `n_remain`/rate, a session
  **footnote** (avg tok/s + KV-cache reuse) from `/metrics` (~2 s background),
  and a **sparkline** of the last N live tok/s samples (`rate_hist`). None of
  these affect `conn`; all hidden until their data arrives (and the
  sparkline/footnote are hidden entirely in the `Down` state).
- **The card is the approved design**: a dark translucent rounded card
  painted with the painter into a **borderless, transparent, always-on-top**
  window (no window chrome). Status dot + big number left, model + capacity
  right-aligned, leftover line, sparkline + footnote, dim host footer;
  right-click menu (Quit).
- `widget_state.log` now logs `host / phase / toks`. `cargo test` = 19 tests
  (15 pure state + parse incl. sparkline lifecycle, 2 live `/slots` +
  `/props`) plus the golden-screenshot test (4 phase scenes). Verified
  end-to-end against `tools/mock_server.py cycle`: prefill→`Prompting`,
  decode→`Generating` at ~95 tok/s, idle, and the leftover/footnote lines.
  See §14.
- *Exit (met):* the desktop shows the correct phase + live number and degrades
  gracefully when the server is stopped.
- *Still M3:* the **interaction** — drag-to-move, compact mode, tray
  show/hide, config persistence + restore, single-instance guard.

**M3 — Interaction + persistence**
- The card (chrome + content) is done — **multi-server, both display
  modes**: the data card (one row per configured server, stacked, §5) and
  the combined-activity scene ("Plumber's run": one level band per server,
  pre-rendered `scene_assets` bitmaps in `shared/scene-assets/`. The server list + poll cadence come from the
  **config file** (`src/config.rs`, §7) — two servers by default.
- The window is borderless, so the top-right **close ×** is the visible
  window control (right-click menu: switch mode / Quit). The **mode toggle
  is not a button** — a click on the card content flips data ⇄ scene.
  Goldens cover both modes × 4 phases (8 images, 640×820 @2x).
- **Done in M3:** the top **drag strip** (`ViewportCommand::StartDrag` →
  winit `drag_window()`), the visible top-right close × (opaque chip,
  right-click menu), the ellipsized/anchored data-card model
  line that keeps long live model names clear of the corner chrome, and the
  **multi-server vertical scale** (window height grows with the config).
- What's left: double-click full ⇄ compact, tray show/hide, window-position
  persistence + restore, single-instance guard.
- *Exit:* full MVP widget behavior as described in §1, in a frameless card.

**M4 — Polish (post-MVP)**
- Mica/Acrylic blur behind the translucent card (egui has no Mica — needs a
  compositing hook or a pre-rendered background sampled from the desktop).
- Per-monitor always-on-top + auto-start at Windows login.
- Optional single-`.exe` packaging.

---

## 11. Out of scope (for now)

- Click-through / transparent-to-mouse modes.
- Aggregating the fleet into a single summary number (the scene already shows
  combined activity per-server; a roll-up stat is a future add).
- TLS to the server.
- Any non-visual feedback (sound, toast).
- Full Win11 Mica theme (post-MVP; M4).

---

## 12. Open questions

1. **Default look** — dark compact pill (96×96) or the 260×120 card? And a
   default corner (top-right is typical)?
2. **Accent color** — any preference, or keep `#4ec9b0`?
3. **Auto-start at login** — Startup folder / Task Scheduler, or manual launch?
4. **Release packaging** — is a `release` build (small `.exe`, `~5–10 MB`)
   wanted now, or only when the card is final? (Release needs LTO; see §13.)

---

## 13. Toolchain (this host, no-admin) — PoC setup that works

Environment constraints discovered: **no admin**, **no Visual Studio / MSVC**,
no preinstalled Rust. So the standard MSVC Rust path is unavailable. The
working, all-user-mode setup:

- **Rust (GNU target):** `stable-x86_64-pc-windows-gnu` — needs a C linker,
  which the MSVC target would get from Visual Studio but we don't have.
- **C linker:** portable **WinLibs MinGW-w64 (UCRT)** extracted to
  `C:\Users\m\mingw` (installed via `winget install BrechtSanders.WinLibs.POSIX.UCRT`).

**Installed locations (this machine):**
- `C:\c\Users\m\.cargo\bin` — cargo/rustc (note the literal `C:\c\` — see pitfall)
- `C:\c\Users\m\.rustup` — toolchain
- `C:\Users\m\mingw\mingw64\bin` — gcc, binutils

**Env for every build** (save as `~/.rbenv.sh`, `source` before `cargo`):
```sh
export RUSTUP_HOME="C:\\c\\Users\\m\\.rustup"
export CARGO_HOME="C:\\c\\Users\\m\\.cargo"
export PATH="/c/c/Users/m/.cargo/bin:/c/Users/m/mingw/mingw64/bin:$PATH"
export CC=x86_64-w64-mingw32-gcc
```

**Pitfalls (cost real time; don't repeat):**
1. **`curl -o` fails (curl 23) on MSYS-style `/c/...` paths** with the
   native `/mingw64/bin/curl`. Use a **relative** path or a **`C:\...`**
   path for the output. Piping and status-only `-w` calls are fine.
2. **`rustup-init.sh` chokes** on its internal binary download (uses
   `mktemp` → `/tmp/...`, same MSYS path issue). **Run
   `rustup-init.exe` directly** instead (downloaded with a relative path).
3. **Rustup mis-parsed the MSYS `$HOME`** into `C:\c\Users\m` (it's a
   Windows binary that didn't translate `/c/...`). Rust **still works** from
   there; the env block above points at the literal `C:\c\Users\m` paths.
   Don't "fix" it into a clean path or you'll orphan the toolchain.
4. **eframe/egui 0.36 API moved** (vs. tutorials): the required `App` method
   is **`fn ui(&mut self, ui: &mut Ui, frame)`** (not `update`); the
   top/bottom panel is **`egui::Panel::top(...)`** (not `TopBottomPanel`);
   panels take `&mut Ui`; closing is
   `ctx.send_viewport_cmd(ViewportCommand::Close)` (not `ctx.close()`).
5. **First `cargo build` compiles the whole eframe/winit/glow tree** (~2–3
   min); later builds are seconds. The debug `.exe` is huge (~340 MB,
   debuginfo); `cargo build --release` gives a small self-contained binary.

**Build & run (PoC):**
```sh
source ~/.rbenv.sh
cd widget
cargo run                 # debug, window opens
cargo run --release       # small self-contained exe
```

---

## 14. Testing with the mock server

`tools/mock_server.py` is a stand-in llama-server so the widget can be tested
deterministically — no dependence on whether `m.tower1` is generating
anything. It serves the **same response shapes** as the real server
(`/slots`, `/metrics`, `/props`, `/health`) and reproduces the real
`/metrics` gauge's **lag** (reads `0.0` while generating, shows the last rate
after) so that quirk is testable too. Each busy period **starts with a ~1 s
prefill** (`is_processing` true, `n_decoded` 0) before tokens stream — this is
what exercises the widget's **Prompting** phase.

**Modes:**

| Mode | Behavior |
|---|---|
| `steady [tok/s]` | generates forever at a constant rate (default 95) |
| `cycle [busy idle tok/s]` | alternates busy/idle (default 4 s / 2 s / 95); each busy period has a 1 s prefill |
| `idle` | never generates (server up, nothing running) |
| `fail` | `/slots` returns 500 and `/props` 404 → exercises the `Down` state |

`--port N` overrides the default 11445; `--no-props` makes `/props` return 404
(to test the model line staying hidden).

`/props` returns the **new llama.cpp shape** (`model_alias`, `model_ftype`,
`default_generation_settings.n_ctx`) with a fixed model `mock-llama-8b` /
`Q4_K_M` / `160k`; `/metrics` includes the `prompt_tokens_cached_total` /
`prompt_tokens_total` counters (107407 / 123456 → **87% cached**) for the
session footnote.

**Point the widget at the mock without recompiling:** the server list is the
config file (`%APPDATA%/DeskBuddy/widget.json`, §7) — point its `host` at the
mock. Or override the config path per-run:

```sh
# 1. start the mock (any mode)
python tools/mock_server.py cycle 4 2 95

# 2. point the config at the mock, then run
DESKBUDDY_CONFIG=/path/to/mock.json cargo run
#   mock.json: {"servers":[{"host":"127.0.0.1:11445","label":"mock"}]}

# Windows (PowerShell):
$env:DESKBUDDY_CONFIG="C:\mock.json"; cargo run
```

`tools/run_widget.ps1` does step 2 for the built debug binary.

**What to check** (`widget_state.log`, written per poll):
- `phase` walks `Idle → Prompting → Generating → Idle` each cycle (the
  prefill window shows as `Prompting` with `toks=0.0`).
- `toks` ≈ the mock's configured rate during `Generating` (expect ±a few %:
  derivation is `Δ n_decoded / Δ elapsed` at the adaptive cadence, so 500 ms
  busy polls slightly overshoot).
- Busy polls ~0.5 s apart, idle polls ~2.0 s apart.
- `fail` mode → `phase=Down` and the card's error styling.
- The card shows the model line `mock-llama-8b · Q4_K_M · 160k` and the
  footnote `… · 87% cached` (both hidden under `--no-props` / no `/metrics`).

**Unit tests:** `cargo test` covers the `BuddyState` math (prompting vs
generating, prefill→decode transition, warm-up, counter-reset, `Down` after a
failure, KV-reuse ratio, `/props` non-empty-only) plus parse helpers and a
live `fetch_slots` + `fetch_props` against the real server.
