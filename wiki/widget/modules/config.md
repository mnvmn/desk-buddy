# Module: `config`

Persistent settings for both targets. **The widget's config file is
implemented** (`widget/src/config.rs`); the device side is spec only
(no firmware).

## Widget — implemented

The config is a **JSON file** at
**`%APPDATA%/DeskBuddy/widget.json`** (override the path with the
`DESKBUDDY_CONFIG` env var). It is **written on first run** so there's a file
to edit, and read on every launch. This is where the **server list** and the
**poll cadence** live — adding a server is an edit, not a recompile.

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
| `servers[]` | `m.tower1:11444` (tower1) + `m.mini1:11444` (mini1) | **the server list, in card order** (index 0 = top row). Each is `host` (required, `host:port` — LAN, **not** `127.0.0.1`) + optional `label` (defaults to `host`). |
| `poll.working_ms` | `500` | clamp 200–10 000 |
| `poll.idle_ms` | `2000` | clamp 200–10 000 |
| `poll.error_ms` | `5000` | clamp 200–10 000 |

Rules: missing keys → defaults; an empty/blank server list → the 2-server
default; malformed JSON → ignore the file, use defaults, log a warning
(never crash); numeric cadence clamped to sane bounds (`clamp_ms`).

`Config::load()` returns the parsed config (or defaults); `Config::default()`
is the 2-server default list (tower1 → `m.tower1`, mini1 → `m.mini1`) + default
cadence. `config_path()` resolves
`DESKBUDDY_CONFIG` → `%APPDATA%/DeskBuddy/widget.json`.

Pointing at the [mock](../../../tools/mock_server.py) without recompiling:
write a temp config whose `host` is `127.0.0.1:11445` and set
`DESKBUDDY_CONFIG` to it.

## Device — NVS (spec only)

| Key | Default | Notes |
|---|---|---|
| `wifi.ssid` / `wifi.pswd` | burned in at first flash | set by `tools/provision`, no panel UI in MVP |
| `llama.host` | `m.tower1` (LAN) | **not** `127.0.0.1` — the ESP32 can't reach host loopback |
| `llama.port` | `11444` | |
| `poll_ms_working` / `poll_ms_idle` | 500 / 2000 | clamp 200–10 000 |
| `screensaver.imgA` / `imgB` | built-in defaults | working / idle pictures |

## Dependencies

- **Used by:** [poller](poller.md), [render](render.md), [buddy_state](buddy-state.md)
- **Uses:** filesystem (widget) / NVS (device)
