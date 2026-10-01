# Parity test — widget vs device, same server

Both frontends render the **same `BuddyState` from the same llama.cpp
server**, so given one live server they should agree on: phase
(generating / idle), model name + capacity, and (while generating) a sane
tok/s that advances over time, **and the session token total** (the `NNk total`
chip, from the `/metrics` counters `prompt_tokens_total +
tokens_predicted_total` — both frontends derive it the same way and the device
now renders it in each row's footer). The widget's per-row **session chips are
all on-device now** too: the **`×N slots`** capacity-line chip (from the
`/slots` length), the **`NN% cached`** footnote (`prompt_tokens_cached_total /
prompt_tokens_total`, clamped 0–100), the **`spec NN%`** footnote (speculative
acceptance `accepted / draft`), the **`avg NN`** footnote
(`predicted_tokens_seconds` gauge — hidden when ~0, i.e. idle, exactly like the
widget). (The widget's **tok/s sparkline** is widget-only — the device has no
equivalent and it is *not* a parity item.) The
**`prompting` prefill-% headline** is on-device too: while a prompt is still
processing the big number is `n_prompt_tokens_processed / n_prompt_tokens ×
100` (the same formula the widget uses), with the leftover line reading
`processing Nk/Nk tok`. This test captures
**both at the same time**
— the widget deterministically in each scene, the ESP32 device *live* while
a real generation runs — and produces a side-by-side contact sheet plus a
machine-readable report.

## What it does

1. **Widget side** — for each scene (`generating`, `idle`, `down`,
   `prompting`), spawns `deskbuddy_widget.exe` with
   `DESKBUDDY_SCENE=<scene>` + `EFRAME_SCREENSHOT_TO=<out.png>` (the same
   deterministic-render hook as `widget/tests/screenshot_test.rs`), then
   checks the fresh capture against the committed golden in
   `widget/tests/snapshots/` (mean per-channel diff < 1.5).
3. **Device side** — opens COM3 and drives the firmware:
   - waits for **two consecutive `poll: idle`** lines, sends `CAP`, and
     composites the 480×480 panel from the streamed tiles → `device_idle_*.png`;
   - fires a long generation at `m.tower1:11444`
     (`/v1/chat/completions`, stream, ~3000 tokens ≈ 30–40 s), then for each
     device poll that reports `WORKING` sends `CAP` and composites one more
     frame, spaced ~1.1 s apart so the on-panel numbers advance.
     Default: **3 generating frames**.
4. **Widget LIVE side** (added with the parity defect fixes) — once the
   device has its first mid-generation frame, a hook re-spawns
   `deskbuddy_widget.exe` with **`DESKBUDDY_LIVE_SERVER=m.tower1:11444`**
   (no scene). The widget seeds its `BuddyState` from the *live* server —
   `/props`, `/metrics`, and two `/slots` snapshots 800 ms apart for a real
   tok/s — so it reads the **same server at the same moment** the device
   frame was captured. Saved as `widget/widget_live.png` and shown as a
   `widget LIVE` panel on the sheet. This is the true "same input → same
   numbers" comparison (defect P4).
5. **Report** — `runs/<UTC-stamp>/report.json` with per-frame
   validation (rate > 0, `n_remain` strictly decreasing between frames) and
   `runs/<UTC-stamp>/sheet.png`, a 2-column contact sheet: widget scenes +
   the live panel on the left, device frames on the right.

The widget's scene captures and device side run **sequentially in one script**
(the widget binary exits after each capture, so there is no contention). The
scene goldens check the widget's *rendering* fidelity; the **`widget LIVE`
panel** (step 4) is what actually compares the widget against the device on
the *same* live server at the *same* moment — the two tok/s numbers on the
sheet should be within one poll's worth of drift of each other.

## Run it

```bash
# full run (widget scenes + device idle + 3 generating frames), ~2-5 min
python tests/parity/parity_test.py

# widget only (no serial, no generation) — fast CI-ish check
python tests/parity/parity_test.py --widget-only

# device only (skip the widget captures)
python tests/parity/parity_test.py --device-only

# fewer device frames / no idle frame / rebuild the widget first
python tests/parity/parity_test.py --frames 2 --skip-idle --build
```

Prereqs: widget binary built (`--build` or `cargo build` in `widget/`),
`pyserial` + `Pillow` (host side only), device plugged in on `--port`
(default `COM3`), `llama-server` up on `m.tower1:11444` (checked at start;
the run continues and the device frames will show down otherwise).

## Reading the output

- `=== parity summary ===` at the end: widget `OK`/`MISMATCH` per scene,
  one line per device frame with its raw `poll:` line, and device
  `validation` (empty list = OK).
- `report.json`: everything above, plus exact poll values (rate, `ctx_left`,
  `remain`) per frame.
- `sheet.png`: eyeball the two frontends. Expected: same phase colors, same
  model/capacity text, device rate near the server's real tok/s, `n_remain`
  visibly shrinking frame to frame.

## What is (deliberately) not compared pixel-to-pixel

The two frontends are **different layouts** on different canvases (widget:
320×~392, 2 server rows, Rust/egui; device: 448×448 single row, LVGL/Mont-
serrat). So parity is validated at the *data* level — phase, model, rate,
remaining tokens — while the widget's pixel fidelity is enforced against its
own goldens. If a future milestone makes the layouts pixel-aligned, this
test is the right place to add a resampled pixel diff.

## Gotchas

- **The poll line arrives *after* the tiles, not before.** The firmware
  prints `poll: ...` once per poll iteration (500 ms while working, 2 s
  idle, 5 s on error — spec §3.3), but the render for that poll happens in
  the *next* iteration — so the line that matches what is on the panel when
  you send `CAP` lands in the serial buffer *during* the tile stream (or
  right after `END`). The capture code therefore tracks the latest `poll:`
  seen while draining the stream, then does a short tail read. If you see
  `rate 0.0` on every frame in the report, the poll line pairing is broken —
  check that path first.
- **Never `export EFRAME_SCREENSHOT_TO` in your shell** and leave it set —
  the terminal session persists it and every later "live" widget launch dies
  silently in one-shot screenshot mode. The script only sets it in child
  envs.
- The device frame pacing keys off the firmware's **own `poll:` lines** on
  the serial console, not a wall-clock guess — so a slow poll (bad Wi-Fi)
  just spaces the frames out instead of misfiring.
- A generation already running on the server is *adopted*: the script only
  fires a new one when none is in flight.
- Runs are timestamped (`runs/<UTC>/`) and never overwrite each other; old
  runs are safe to delete.
- The firmware's `CAP` command re-streams the whole framebuffer
  (~480×480 RGB565 base64 over CDC ≈ 1–2 s per frame); each capture
  temporarily blocks the device's LVGL lock, which is why frames are
  spaced at least one full poll apart.

## Layout

```
tests/parity/
├── README.md         # this file
├── parity_test.py    # the whole test (widget + device + report)
├── .gitignore        # runs/, __pycache__, _parity_*.png
└── runs/<stamp>/     # one dir per run
    ├── report.json   # machine-readable results + validation
    ├── sheet.png     # side-by-side contact sheet (incl. widget LIVE)
    ├── widget/widget_<scene>.png
    ├── widget/widget_live.png      # DESKBUDY_LIVE_SERVER capture (P4)
    └── device/device_<phase>_<ts>.png
```

Related: `wiki/shared/parity/defects-2026-09-26.md` (the 5 parity defects found by
this test and the fixes they drove); widget golden test
`widget/tests/screenshot_test.rs` (the deterministic render this reuses);
device capture helpers `esp32c6_480x480/firmware/tools/capture_panel.py` (boot
capture) and `capture_live.py` (manual live capture this automates);
`wiki/esp32c6_480x480/firmware-build.md` (build/flash recipe).
