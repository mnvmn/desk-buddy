# Getting Started

The **widget** is implemented (M3, Rust/eframe) and runnable now. The **device**
(ESP32) is still at the spec stage.

## Run the widget (implemented)

**Prereqs (this host):** Rust **GNU** target + WinLibs MinGW-w64, all
user-mode (no admin / no MSVC). Env for every build is `~/.rbenv.sh` — see
[`../widget/WINDOWS_WIDGET_SPEC.md` §13](../../widget/WINDOWS_WIDGET_SPEC.md)
for the exact paths and the MSYS-path pitfalls.

```sh
source ~/.rbenv.sh
cd widget
cargo run                 # debug build, window opens, polls the configured servers
cargo test                # 35 unit tests + 1 golden-screenshot test (8 scenes)
cargo build --release     # small self-contained .exe
```

The built binary also runs directly:
`widget/target/debug/deskbuddy_widget.exe`.

**Point it at different servers / the mock without recompiling:** edit the
config file (`%APPDATA%/DeskBuddy/widget.json` — the server list + poll
cadence), or override the config path per-run:
```sh
# bash
DESKBUDDY_CONFIG=/path/to/mock.json cargo run
# PowerShell
$env:DESKBUDDY_CONFIG="C:\mock.json"; cargo run
#   mock.json: {"servers":[{"host":"127.0.0.1:11445","label":"mock"}]}
```
Default (no file) is two servers at `m.tower1:11444`.

## Test with the mock server (implemented)

`tools/mock_server.py` is a deterministic stand-in llama-server serving the
same `/slots`/`/metrics`/`/props` shapes (and reproducing the `/metrics`
gauge lag). Each busy period starts with a ~1 s prefill to exercise the
**Prompting** phase. Full guide:
[`../widget/WINDOWS_WIDGET_SPEC.md` §14](../../widget/WINDOWS_WIDGET_SPEC.md).

```sh
python tools/mock_server.py cycle 4 2 95      # 4 s busy / 2 s idle @ 95 tok/s
# then in another shell — point the config at the mock:
DESKBUDDY_CONFIG=/tmp/mock.json cargo run
#   /tmp/mock.json: {"servers":[{"host":"127.0.0.1:11445","label":"mock"}]}
```
The widget appends one line per poll to `widget_state.log` (one phase token
per server, e.g. `[00001] generating | [00002] idle`) — grep it to verify the
live path (`Idle → Prompting → Generating → Idle`, ~95 tok/s, 0.5 s busy /
2.0 s idle cadence) without watching the window.
Other modes: `steady [tok/s]`, `idle`, `fail` (exercises the `Down` phase);
`--no-props` hides the model line.

## Screenshot (golden-image) tests (implemented)

Rust/egui has no Playwright, but eframe ships the `__screenshot` feature:
when the binary runs with `EFRAME_SCREENSHOT_TO=<path>` set, the 2nd render
pass writes real GPU pixels to a PNG and exits 0. `tests/screenshot_test.rs`
wraps that — one test spawns the binary once per (phase × mode) scene
(`DESKBUDDY_SCENE=generating|prompting|idle|down`, which seeds a
fully-deterministic `WidgetState` — one `BuddyState` per configured server —
and skips the poller) and compares the PNG against
`tests/snapshots/<scene>.png` (data card) / `scene_<scene>.png` (scene mode)
pixel-by-pixel (anti-aliasing tolerance built in). **8 goldens**: 4 phases ×
(data card + scene), captured at 640×820 (2 servers @ 2x).

```sh
cargo test --test screenshot_test   # compare against goldens
UPDATE_GOLDENS=1 cargo test --test screenshot_test   # regenerate after an
                                                     # intentional visual change
```

The pixel ratio is pinned to 2.0 in screenshot mode, so goldens are stable
across machines that share the eframe version and font set. The card is
painted entirely with the painter (`painter.text` / `circle_filled` /
`rect_filled` at computed positions) into a transparent, undecorated window —
the egui layout helpers (`vertical_centered_justified`, `horizontal_centered`,
nested `with_layout(RightToLeft,…)`) are off-canvas traps that the test
surfaced, and round bullets are missing glyphs in the default font, so the
status dot is a *painted* circle.

## Server side (both targets)

- `llama-server` (llama.cpp) running on the host, **bound to the LAN**:
  `--host 0.0.0.0` (or the host's LAN IP) — *not* loopback only. The ESP32
  cannot reach `127.0.0.1`; the widget works either way but the shared
  default is the LAN hostname.
- Port `11444` (default) reachable; firewall must allow inbound TCP `11444`.
- `/slots` is always on; `/metrics` only with `--metrics` (this server has it,
  but the widget does **not** use the gauge — it lags).
- Verify any time:
  ```sh
  curl -s -o /dev/null -w '%{http_code}' http://m.tower1:11444/slots   # expect 200
  ```
  Full re-verify: [shared/API_ENDPOINTS.md](../../shared/API_ENDPOINTS.md).

## Device target (spec stage — setup for when M1 firmware lands)

- Waveshare ESP32-C6-Touch-AMOLED-2.16, **`-EN`** variant (wired USB-C).
- Arduino IDE + arduino-esp32 ≥ 3.3.0 **or** ESP-IDF v5.5.x (Arduino
  recommended — the bundled LVGL v9 example makes display+touch nearly free).
- Start from Waveshare's `09_LVGL_V9_Test` Arduino example.
- **Still open:** the KEY button GPIO is not in the bundled examples — read it
  off the schematic before `button` can be written.

## Configuration

| Where | What |
|---|---|
| Widget (M3, implemented) | `%APPDATA%/DeskBuddy/widget.json` (override path with `DESKBUDDY_CONFIG`) — **server list** (`servers[]` of `{host, label}`) + **poll cadence** (`poll.{working,idle,error}_ms`) |
| Device (spec) | NVS: `wifi.*`, `llama.host/port`, `poll_ms_*`, screensaver images — set by `tools/provision` |

Missing keys → defaults; an empty server list → the 2-server default;
malformed config → defaults + warning, never a crash. Default `host` is
`m.tower1` — **never** `127.0.0.1` for the device.

## Where to go next

- Architecture + design decisions: [architecture.md](../shared/architecture.md)
- Per-module reference: [README.md#module-map](README.md#module-map)
- Endpoint/field details: [shared/API_ENDPOINTS.md](../../shared/API_ENDPOINTS.md)
- Widget spec (milestones, interaction, mock §14): [../widget/WINDOWS_WIDGET_SPEC.md](../../widget/WINDOWS_WIDGET_SPEC.md)
- Device spec (milestones, hardware): [../esp32c6_480x480/PRODUCT_SPEC.md](../../esp32c6_480x480/PRODUCT_SPEC.md)
