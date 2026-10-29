# DeskBuddy Wiki

> Reference docs (what/how) for the DeskBuddy project, **split by target**:
> [`desktop-llama/`](../desktop-llama/) for the Windows desktop widget (**implemented,
> M3** — Rust/eframe under `desktop-llama/`, multi-server vertical card),
> [`amoled-panel-llama/`](../amoled-panel-llama/) for the ESP32-C6 AMOLED desk
> panel (**implemented — M1-static card + plumber scene on the real panel**),
> [`epaper-panel-weather/`](../epaper-panel-weather/) for the ESP32-S3 5.79" e-paper
> panel (**bring-up done** — net-status page + live HTTPS weather). Docs all
> targets share — the architecture, this index, and the widget-vs-device
> parity test — live here in `shared/`. Module docs are marked with their
> real status.

## Target indexes

- **Windows widget (implemented):** [../desktop-llama/](../desktop-llama/) — build & run,
  the multi-server data-driven card + combined-activity scene, state machine,
  poller, config, screenshots
- **ESP32-C6 AMOLED (implemented, M1-static):** [../amoled-panel-llama/](../amoled-panel-llama/)
  — the static card, plumber scene states, build/flash recipe, LVGL gotchas
- **ESP32-S3 e-paper (bring-up):** [../epaper-panel-weather/](../epaper-panel-weather/)
  — flash/verify results, 1-bit render gotchas, the weather mock defect log
- **Parity test (widget vs C6 device):** [parity/README.md](parity/README.md)
  — capture widget + device from the same live server, compare state

## Sources

- [`desktop-llama/src/`](../../desktop-llama/src/) — implemented Rust widget
  (`buddy_state.rs`, `poller.rs`, `config.rs`, `main.rs` + generated
  `scene_assets`)
- [`shared/scene-assets/`](../../shared/scene-assets/README.md) — the scene
  bitmaps (source of truth for the device targets; `render_scene.py`
  regenerates)
- [`tools/mock_server.py`](../../tools/mock_server.py) — deterministic
  llama-server stand-in for widget tests
- [`amoled-panel-llama/PRODUCT_SPEC.md`](../../amoled-panel-llama/PRODUCT_SPEC.md)
  — ESP32-C6 AMOLED device spec
- [`epaper-panel-weather/README.md`](../../epaper-panel-weather/README.md) — ESP32-S3
  CrowPanel e-paper: device facts, pin map, flash recipe
- [`desktop-llama/WINDOWS_WIDGET_SPEC.md`](../../desktop-llama/WINDOWS_WIDGET_SPEC.md) —
  Windows widget spec
- [`shared/API_ENDPOINTS.md`](../../shared/API_ENDPOINTS.md) — live endpoint
  probe

## Module Map

| Module | Target | Status | Purpose |
|---|---|---|---|
| [`buddy-state`](../desktop-llama/modules/buddy-state.md) | shared (impl: widget) | ✅ `buddy_state.rs` | Per-server state machine + tok/s math; `WidgetState` multi-server wrapper (pure, unit-tested) |
| [`poller`](../desktop-llama/modules/poller.md) | shared (impl: widget) | ✅ `poller.rs` | Adaptive multi-server HTTP polling: `/slots` hot path + `/props` + `/metrics` on one background thread |
| [`render`](../desktop-llama/modules/render.md) | widget | ✅ M3 impl (`main.rs`) | eframe/egui floating card — per-server rows + scene bands (blits `scene_assets` bitmaps) |
| [`config`](../desktop-llama/modules/config.md) | widget + C6 NVS | 🔶 partial | Widget JSON config file (server list + cadence) implemented; C6 NVS + S3 panel config planned |
| [`display-bsp`](../amoled-panel-llama/modules/display-bsp.md) | C6 device | ✅ impl (`bsp_lvgl_port`) | sh8601 4-bit panel bring-up, LVGL lock, flush rotation (live bring-up lives in [firmware-build.md](../amoled-panel-llama/firmware-build.md)) |
| [`ui`](../amoled-panel-llama/modules/ui.md) | C6 device | ✅ impl (`firmware.ino`) | data card + plumber scene screens |
| [`input`](../amoled-panel-llama/modules/input.md) | C6 device | ✅ impl | CST9220 touch (scene ⇄ card) + serial command console |
| [S3 bring-up](../epaper-panel-weather/bringup.md) | S3 device | ✅ M0 done | E-paper flash/verify, net-status page, live HTTPS weather; buddy-state UI not started |

Status key: ✅ implemented · 🔶 partially implemented · ⬜ spec only.

## Documents

- [architecture.md](architecture.md) — system diagram, data flow, design
  decisions (shared by the widget + C6 target; the S3 panel is not yet on
  the buddy-state data path)
- Widget: [getting-started.md](../desktop-llama/getting-started.md) (build, run,
  mock testing, screenshots),
  [diagrams/sequences.md](../desktop-llama/diagrams/sequences.md) (poll loop, error
  recovery, mode toggle),
  [diagrams/class-diagram.md](../desktop-llama/diagrams/class-diagram.md) (core types)
- C6 device: [README.md](../amoled-panel-llama/README.md) (milestones, hardware,
  open questions from the product spec),
  [firmware-build.md](../amoled-panel-llama/firmware-build.md) (build/flash
  recipe + capture scripts)
- S3 device: [README.md](../epaper-panel-weather/README.md) (bring-up status),
  [bringup.md](../epaper-panel-weather/bringup.md) (results + gotchas),
  [defects.md](../epaper-panel-weather/defects.md) (weather mock D1–D16)
- Parity: [parity/README.md](parity/README.md),
  [parity/defects-2026-09-26.md](parity/defects-2026-09-26.md)
