# Windows Widget (implemented — M3)

> Status: **implemented in Rust/eframe** (`widget/src/`). M2 is done
> (the data-driven floating card) and **M3 is largely done**: multi-server
> vertical card, config-file-driven server list + poll cadence, both display
> modes (data card + the combined-activity "Plumber's run" scene), drag
> strip + chrome buttons. Remaining: compact, tray, position persistence.
> Spec:
> [`../WINDOWS_WIDGET_SPEC.md`](../../widget/WINDOWS_WIDGET_SPEC.md).
> Shared docs: [../shared/](../shared/).

## Documents

- [getting-started.md](getting-started.md) — prerequisites, build & run,
  mock testing, golden-screenshot tests
- [modules/](modules/) — per-module reference:
  - [buddy-state.md](modules/buddy-state.md) — `BuddyState` phase machine +
    tok/s math, and the multi-server `WidgetState` wrapper (the one
    shared-logic piece; the device mirrors it in C)
  - [poller.md](modules/poller.md) — adaptive multi-server HTTP thread
    (`/slots` hot path per server, `/props`, `/metrics`; fetch-without-lock)
  - [render.md](modules/render.md) — the egui floating card (per-server rows,
    painter layout, palette, scene mode, screenshot test mode)
  - [config.md](modules/config.md) — the JSON config file: server list + poll
    cadence (implemented, `%APPDATA%/DeskBuddy/widget.json`)
  - [scene-design.md](modules/scene-design.md) — the "Plumber's Run" scene
    design concept + the consistency/engagement brainstorm
- [diagrams/](diagrams/) —
  [sequences.md](diagrams/sequences.md) (poll cycle, error recovery, mode
  toggle), [class-diagram.md](diagrams/class-diagram.md) (core types)
- `desk-buddy-card-design.png` — the approved card design the M2/M3 card
  matches (not in the repo yet)
