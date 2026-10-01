# DeskBuddy

A desktop companion for a local [`llama.cpp`](https://github.com/ggml-org/llama.cpp)
(`llama-server`) instance. One question, answered at a glance:
**is my model generating right now, and how fast?**

Two frontends share one data source and the same tok/s math — same
`BuddyState` model, same parsing rules, same polling cadence:

| Subproject | What it is | Spec |
|---|---|---|
| [`esp32c6_480x480/`](esp32c6_480x480/PRODUCT_SPEC.md) | ESP32-C6 + 2.16" 480×480 AMOLED desk panel (LVGL v9) | [esp32c6_480x480/PRODUCT_SPEC.md](esp32c6_480x480/PRODUCT_SPEC.md) |
| [`esp32s3_272x792/`](esp32s3_272x792/README.md) | ESP32-S3 + 5.79" 272×792 e-paper panel (CrowPanel, 1-bit) — bring-up | [esp32s3_272x792/README.md](esp32s3_272x792/README.md) |
| [`widget/`](widget/WINDOWS_WIDGET_SPEC.md) | Floating always-on-top card on the Windows 11 desktop (Rust/eframe, **implemented M2**) | [widget/WINDOWS_WIDGET_SPEC.md](widget/WINDOWS_WIDGET_SPEC.md) |

Both poll the same endpoint — `GET /slots` on the host's **LAN address**
(default `m.tower1:11444`, *not* `127.0.0.1`). Endpoint + field reference,
live-probed 2026-09-14: [`shared/API_ENDPOINTS.md`](shared/API_ENDPOINTS.md).

## Key concepts

- **`BuddyState`** — one struct drives the whole UI: connection state,
  `working` flag, derived tok/s, display mode. See [wiki/widget/modules/buddy-state.md](wiki/widget/modules/buddy-state.md).
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
├── shared/
│   └── API_ENDPOINTS.md     # live-confirmed endpoint/field reference (both targets)
├── esp32c6_480x480/
│   ├── PRODUCT_SPEC.md      # ESP32 AMOLED device spec
│   ├── firmware/            # Arduino/PlatformIO project (to build — M1)
│   └── tools/               # provision + img2buddy helpers (to build — M1/M3)
├── widget/
│   ├── WINDOWS_WIDGET_SPEC.md  # Windows widget spec (milestones, mock §14)
│   ├── src/                 # implemented Rust/eframe widget (M2)
│   └── tests/               # unit + golden-screenshot tests
└── wiki/                    # reference docs, split by target
    ├── shared/              # index (README.md) + architecture.md + parity/ (widget vs C6)
    ├── widget/              # getting-started, modules/, diagrams/ (implemented)
    ├── esp32c6_480x480/     # README, firmware-build, modules/ (C6 AMOLED device)
    └── esp32s3_272x792/     # README, bringup, defects (S3 e-paper device)
```

## Status

The **widget is implemented (M2)** in Rust/eframe — the data-driven
floating card, live tok/s, prompting/idle/down states, sparkline, session
footnote (see its [milestones](widget/WINDOWS_WIDGET_SPEC.md#10-milestones));
M3 (drag, compact, tray, persistence) is next. The **device is still at
spec stage**: M1 proves connectivity (serial tok/s), M2 renders the number,
M3 adds the screensaver + button, M4 is polish.
See [wiki/shared/README.md](wiki/shared/README.md) for the full module map.

## Where to go next

- New here → [wiki/widget/getting-started.md](wiki/widget/getting-started.md)
- How it fits together → [wiki/shared/architecture.md](wiki/shared/architecture.md)
- Per-module reference → [wiki/shared/README.md](wiki/shared/README.md#module-map)
- Endpoint/field details → [shared/API_ENDPOINTS.md](shared/API_ENDPOINTS.md)
