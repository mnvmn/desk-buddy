# DeskBuddy

A desktop companion for a local [`llama.cpp`](https://github.com/ggml-org/llama.cpp)
(`llama-server`) instance. One question, answered at a glance:
**is my model generating right now, and how fast?**

Two frontends share one data source and the same tok/s math — same
`BuddyState` model, same parsing rules, same polling cadence:

| Subproject | What it is | Spec |
|---|---|---|
| [`amoled-panel-llama/`](amoled-panel-llama/PRODUCT_SPEC.md) | ESP32-C6 + 2.16" 480×480 AMOLED desk panel (LVGL v9) — **live card + state-following scene on the panel** | [amoled-panel-llama/PRODUCT_SPEC.md](amoled-panel-llama/PRODUCT_SPEC.md) |
| [`epaper-panel-weather/`](epaper-panel-weather/README.md) | ESP32-S3 + 5.79" 272×792 e-paper panel (CrowPanel, 1-bit) — **v2 buddy-state UI, verified on hardware** | [epaper-panel-weather/README.md](epaper-panel-weather/README.md) |
| [`desktop-llama/`](desktop-llama/WINDOWS_WIDGET_SPEC.md) | Floating always-on-top card on the Windows 11 desktop (Rust/eframe, **implemented M2**) | [desktop-llama/WINDOWS_WIDGET_SPEC.md](desktop-llama/WINDOWS_WIDGET_SPEC.md) |

Both poll the same endpoint — `GET /slots` on the host's **LAN address**
(default `m.tower1:11444`, *not* `127.0.0.1`). Endpoint + field reference,
live-probed 2026-09-14: [`shared/API_ENDPOINTS.md`](shared/API_ENDPOINTS.md).

## Key concepts

- **`BuddyState`** — one struct drives the whole UI: connection state,
  `working` flag, derived tok/s, display mode. See [wiki/desktop-llama/modules/buddy-state.md](wiki/desktop-llama/modules/buddy-state.md).
- **`/slots` first, `/metrics` second** — `GET /slots` works on any stock
  server; the `predicted_tokens_seconds` gauge from `/metrics` is an optional
  smoother source (only when the server runs with `--metrics`).
- **Idle ≠ down** — server up but nothing generating is a healthy `IDLE`
  state; unreachable endpoint is `SERVER_DOWN`. Never conflate them.
- **Defensive parsing** — `next_token` is an *array*, the `params` block is
  large and unused. Any parse failure degrades to `SERVER_DOWN`; never crash.

## Repo layout

```
desk-buddy/
├── README.md                # this file
├── AGENTS.md                # agent working agreements for this repo
├── shared/
│   ├── API_ENDPOINTS.md     # live-confirmed endpoint/field reference (all targets)
│   ├── net_config.h         # shared network/server config (ESP32 targets)
│   ├── net.env{,.example}   # probe/network credentials (env, gitignored values)
│   ├── scene-assets/        # plumber scene bitmaps shared by widget + C6 panel
│   └── tools/               # probe.sh — one-command server re-verify
├── amoled-panel-llama/
│   ├── PRODUCT_SPEC.md      # ESP32 AMOLED device spec (design record)
│   └── firmware/            # Arduino sketch: live card + scene, LVGL v9 (flashed)
├── epaper-panel-weather/
│   ├── README.md            # S3 device status + map
│   ├── fw/                  # firmware (weather + buddy-state v2 UI, flashed)
│   ├── web/                 # v2 UI prototype (panel_v2.html + loader dir)
│   ├── case/                # printable case docs + shots
│   └── tools/               # panel tooling (bitmap convert, flash helpers)
├── desktop-llama/
│   ├── WINDOWS_WIDGET_SPEC.md  # Windows widget spec (milestones, mock §14)
│   ├── src/                 # implemented Rust/eframe widget (M2)
│   └── tests/               # unit + golden-screenshot tests
├── tests/
│   └── parity/              # cross-target render parity suite (widget vs C6)
├── tools/
│   ├── mock_server.py       # local /slots mock for offline runs
│   └── run_widget.ps1       # one-command widget launch (mock or live)
└── wiki/                    # reference docs, split by target
    ├── shared/              # index (README.md) + architecture.md + parity/
    ├── desktop-llama/       # getting-started, modules/, diagrams/ (implemented)
    ├── amoled-panel-llama/  # README, firmware-build, modules/ (C6 AMOLED device)
    └── epaper-panel-weather/# README, bringup, defects (S3 e-paper device)
```

## Status

All three targets are **implemented and on their hardware**:

- **Widget** (Rust/eframe, M2): data-driven floating card, live tok/s,
  prompting/idle/down states, sparkline, session footnote, plumber scene
  bands — plus the golden-screenshot regression gate
  (see its [milestones](desktop-llama/WINDOWS_WIDGET_SPEC.md#10-milestones));
  M3 (drag, compact, tray, persistence) is next.
- **C6 AMOLED panel**: live desk-buddy card (polls `/slots` + `/metrics`)
  and the state-following plumber scene; touch + serial commands drive the
  screens. Build/flash: [wiki/amoled-panel-llama/firmware-build.md](wiki/amoled-panel-llama/firmware-build.md).
- **S3 e-paper panel**: v2 buddy-state UI (weather + server state), verified
  on hardware; [epaper-panel-weather/README.md](epaper-panel-weather/README.md).

See [wiki/shared/README.md](wiki/shared/README.md) for the full module map.

## Where to go next

- New here → [wiki/desktop-llama/getting-started.md](wiki/desktop-llama/getting-started.md)
- How it fits together → [wiki/shared/architecture.md](wiki/shared/architecture.md)
- Per-module reference → [wiki/shared/README.md](wiki/shared/README.md#module-map)
- Endpoint/field details → [shared/API_ENDPOINTS.md](shared/API_ENDPOINTS.md)
