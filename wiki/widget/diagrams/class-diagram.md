# Class Diagram

Core types. The widget is **implemented in Rust** (`widget/src/`); the device
will mirror the same shape in C (spec only). The widget is **multi-server**:
one `BuddyState` per configured server, wrapped in `WidgetState`.

```mermaid
classDiagram
    class ConnState {
        <<enum>>
        Up
        Down
    }
    class Phase {
        <<enum>>
        Down
        Idle
        Prompting
        Generating
    }
    class DisplayMode {
        <<enum>>
        Data
        Scene
    }
    class PollTuning {
        +u64 working_ms
        +u64 idle_ms
        +u64 error_ms
    }
    class BuddyState {
        +ConnState conn
        +bool working
        +f64 toks_per_s
        +i64 n_remain
        +u64 decoded
        +Option~String~ host_label
        -Option~u64~ last_decode_total
        +feed_ok(is_processing, decode_total, n_remain, elapsed)
        +feed_error()
        +feed_props(model, quant, ctx)
        +feed_metrics(avg, cached, total)
        +phase() Phase
        +poll_interval(tuning) Duration
    }
    class WidgetState {
        +Vec~BuddyState~ servers
        +Vec~String~ labels
        +DisplayMode display_mode
        +toggle_mode()
    }
    class ServerCfg {
        +String host
        +Option~String~ label
    }
    class Config {
        +Vec~ServerCfg~ servers
        +PollTuning poll
        +load() Config
    }
    class SlotData {
        <<tuple (working, decode_total, n_remain)>>
        +bool working
        +u64 decode_total
        +i64 n_remain
    }
    class PollerThread {
        +state : SharedState
        +ctx : egui.Context
        +cfg : Config
        +fetch_slots(host) Result~SlotData~
        +spawn_poller(state, ctx, config) JoinHandle
    }
    class Scene {
        +paint_scene_combined(phases, frame, rect) Mesh
        +paint_band(mesh, phase, frame, rect, radius)
    }
    class BuddyApp {
        +state : SharedState
        +bool closed
        +card_height(n) i32
        +draw_data_card(ui)
        +draw_scene(ui)
        +ui(ui, frame)
    }

    BuddyState --> ConnState : has
    BuddyState --> PollTuning : cadence via
    WidgetState *-- BuddyState : one per server
    WidgetState --> DisplayMode : shared mode
    Config *-- ServerCfg : list
    Config --> PollTuning : poll
    PollerThread --> WidgetState : feeds (Arc/Mutex)
    PollerThread --> Config : reads servers + cadence
    PollerThread --> SlotData : derives per poll per server
    BuddyApp --> WidgetState : reads (clone) each frame
    BuddyApp --> Config : sizes card from servers
    BuddyApp --> Scene : paint_scene_combined
    Scene ..> BuddyState : one band per phase
```

## Notes

- `BuddyState` is the **per-server** state — the one piece of shared logic the
  device mirrors in C. `WidgetState` is the widget-only wrapper: `Vec<BuddyState>`
  (one per configured server) + labels + a single shared `DisplayMode`.
  `SharedState = Arc<Mutex<WidgetState>>` is the poller↔UI hand-off.
- `PollTuning` holds the config-driven cadence (working/idle/error ms, clamped
  200–10 000). The poller thread sleeps the **minimum** `poll_interval(tuning)`
  across all servers.
- `Config` is the JSON file (`%APPDATA%/DeskBuddy/widget.json`): the
  `servers[]` list (index 0 = top row) + `poll` cadence.
- `ConnState` is two-variant on the desktop: `Up` / `Down`. **Idle is
  `conn == Up && working == false`, not a `ConnState` variant.** The device adds
  `OFFLINE_WIFI` (Wi-Fi down is detected before any HTTP).
- **Threading (widget):** the poller is a real `std::thread`; `WidgetState` is
  shared through `Arc<Mutex<…>>`. The poller **fetches all servers without the
  lock first, then applies under one short lock** and calls
  `ctx.request_repaint()`; the UI reads by locking + cloning each frame. The
  device has no threads — a single FreeRTOS/Arduino loop, so no lock needed.
- `SlotData` is a per-poll value (a tuple), not a runtime object. `n_remain` is
  optional context; `-1` means unbounded. `Scene::paint_scene_combined` is the
  pure band-painter used in scene display mode (one band per server).
