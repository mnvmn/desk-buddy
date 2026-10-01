# DeskBuddy Wiki

> Reference docs (what/how) for the DeskBuddy project, **split by target**:
> [`widget/`](../widget/) for the Windows desktop widget (**implemented,
> M3** — Rust/eframe under `widget/`, multi-server vertical card),
> [`esp32c6_480x480/`](../esp32c6_480x480/) for the ESP32-C6 AMOLED desk
> panel (**implemented — M1-static card + plumber scene on the real panel**),
> [`esp32s3_272x792/`](../esp32s3_272x792/) for the ESP32-S3 5.79" e-paper
> panel (**bring-up done** — net-status page + live HTTPS weather). Docs all
> targets share — the architecture, this index, and the widget-vs-device
> parity test — live here in `shared/`. Module docs are marked with their
> real status.

## Target indexes

- **Windows widget (implemented):** [../widget/](../widget/) — build & run,
  the multi-server data-driven card + combined-activity scene, state machine,
  poller, config, screenshots
- **ESP32-C6 AMOLED (implemented, M1-static):** [../esp32c6_480x480/](../esp32c6_480x480/)
  — the static card, plumber scene states, build/flash recipe, LVGL gotchas
- **ESP32-S3 e-paper (bring-up):** [../esp32s3_272x792/](../esp32s3_272x792/)
  — flash/verify results, 1-bit render gotchas, the weather mock defect log
- **Parity test (widget vs C6 device):** [parity/README.md](parity/README.md)
  — capture widget + device from the same live server, compare state

## Sources

- [`widget/src/`](../../widget/src/) — implemented Rust widget
  (`buddy_state.rs`, `poller.rs`, `config.rs`, `main.rs` + generated
  `scene_assets`)
- [`shared/scene-assets/`](../../shared/scene-assets/README.md) — the scene
  bitmaps (source of truth for the device targets; `render_scene.py`
  regenerates)
- [`tools/mock_server.py`](../../tools/mock_server.py) — deterministic
  llama-server stand-in for widget tests
- [`esp32c6_480x480/PRODUCT_SPEC.md`](../../esp32c6_480x480/PRODUCT_SPEC.md)
  — ESP32-C6 AMOLED device spec
- [`esp32s3_272x792/README.md`](../../esp32s3_272x792/README.md) — ESP32-S3
  CrowPanel e-paper: device facts, pin map, flash recipe
- [`widget/WINDOWS_WIDGET_SPEC.md`](../../widget/WINDOWS_WIDGET_SPEC.md) —
  Windows widget spec
- [`shared/API_ENDPOINTS.md`](../../shared/API_ENDPOINTS.md) — live endpoint
  probe

## Module Map

| Module | Target | Status | Purpose |
|---|---|---|---|
| [`buddy-state`](../widget/modules/buddy-state.md) | shared (impl: widget) | ✅ `buddy_state.rs` | Per-server state machine + tok/s math; `WidgetState` multi-server wrapper (pure, unit-tested) |
| [`poller`](../widget/modules/poller.md) | shared (impl: widget) | ✅ `poller.rs` | Adaptive multi-server HTTP polling: `/slots` hot path + `/props` + `/metrics` on one background thread |
| [`render`](../widget/modules/render.md) | widget | ✅ M3 impl (`main.rs`) | eframe/egui floating card — per-server rows + scene bands (blits `scene_assets` bitmaps) |
| [`config`](../widget/modules/config.md) | widget + C6 NVS | 🔶 partial | Widget JSON config file (server list + cadence) implemented; C6 NVS + S3 panel config planned |
| [`display-bsp`](../esp32c6_480x480/modules/display-bsp.md) | C6 device | ⬜ spec | Waveshare sh8601 4-bit panel bring-up wrapper (live bring-up lives in [firmware-build.md](../esp32c6_480x480/firmware-build.md)) |
| [`ui`](../esp32c6_480x480/modules/ui.md) | C6 device | ⬜ spec | LVGL v9 screens: live, screensaver, error, boot |
| [`button`](../esp32c6_480x480/modules/button.md) | C6 device | ⬜ spec | KEY debounce + Live ⇄ Screensaver toggle |
| [S3 bring-up](../esp32s3_272x792/bringup.md) | S3 device | ✅ M0 done | E-paper flash/verify, net-status page, live HTTPS weather; buddy-state UI not started |

Status key: ✅ implemented · 🔶 partially implemented · ⬜ spec only.

## Documents

- [architecture.md](architecture.md) — system diagram, data flow, design
  decisions (shared by the widget + C6 target; the S3 panel is not yet on
  the buddy-state data path)
- Widget: [getting-started.md](../widget/getting-started.md) (build, run,
  mock testing, screenshots),
  [diagrams/sequences.md](../widget/diagrams/sequences.md) (poll loop, error
  recovery, mode toggle),
  [diagrams/class-diagram.md](../widget/diagrams/class-diagram.md) (core types)
- C6 device: [README.md](../esp32c6_480x480/README.md) (milestones, hardware,
  open questions from the product spec),
  [firmware-build.md](../esp32c6_480x480/firmware-build.md) (build/flash
  recipe + capture scripts)
- S3 device: [README.md](../esp32s3_272x792/README.md) (bring-up status),
  [bringup.md](../esp32s3_272x792/bringup.md) (results + gotchas),
  [defects.md](../esp32s3_272x792/defects.md) (weather mock D1–D16)
- Parity: [parity/README.md](parity/README.md),
  [parity/defects-2026-09-26.md](parity/defects-2026-09-26.md)
