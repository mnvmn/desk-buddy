# Device firmware — build & flash (arduino-cli)

> Status: **M1-static done.** The static desk-buddy card (same layout as the
> widget's data-driven design) flashes and renders on the 480×480 AMOLED;
> serial shows `Display up: static DeskBuddy card` + heartbeats. Next is M1
> live: Wi-Fi + `/slots` poller feeding the card. This is the known-good
> recipe — don't re-derive it.

Toolchain: **arduino-cli + arduino-esp32 core 3.3.3** (matches Waveshare's
bundled `Arduino-v3.3.3` examples). We deliberately moved *off* PlatformIO:
its espressif32 platform has no `platformio-build-esp32c6.py`, so C6 Arduino
builds fail out of the box. arduino-cli is also what Waveshare ships, so the
example code is expected to compile.

## Layout (the part that took a while)

Sketch root is `esp32c6_480x480/firmware/` (the `.ino` must be named `firmware.ino`).
Local libraries live in `esp32c6_480x480/firmware/lib/`. The LVGL library must be the
**Arduino-lib layout** — LVGL root is the lib root, headers at top level:

```
esp32c6_480x480/firmware/
  firmware.ino                 # sketch (name MUST match the folder)
  bsp_lvgl_port.{cpp,h}         # Waveshare LVGL v9 port
  lcd_touch.{cpp,h}
  user_config.h
  src/port_bsp/                # axp2101_bsp, i2c_bsp, etc.
  lib/
    XPowersLib/                # AXP2101 PMIC driver (as shipped)
    lvgl/                      # LVGL 9.3.0 — root IS the lib root
      library.properties       # name=lvgl  (required for detection)
      lv_conf.h                # Waveshare's config (at root)
      src/
        lv_conf.h              # COPIED here too — lv_conf_internal.h does
                               # #include "lv_conf.h" relative to src/
        lvgl.h  core/ draw/ ...
        demos/                 # lv_demos.h (the widget demo)
```

Gotchas that caused real build failures:

- **arduino-cli does NOT scan `lib/` automatically** (the IDE does). You must
  pass `--libraries lib` to `compile` — without it, even a trivial local lib
  shows `candidates: []` and `lvgl.h` is "not found".
- The LVGL root must sit **one level up** so `#include "lvgl.h"`,
  `#include "demos/lv_demos.h"`, and the relative `lv_conf.h` all resolve from
  a single include path. A nested `lib/lvgl/lvgl/...` layout makes demos
  compile from two paths → `multiple definition` link errors.
- `lv_conf.h` is needed in **two** places: `lib/lvgl/lv_conf.h` (root) and
  `lib/lvgl/src/lv_conf.h` (where `lv_conf_internal.h` looks). Keep both in
  sync — they're identical copies of Waveshare's config.
- Trim `lvgl/{tests,examples,docs,zephyr,env_support}` before building — the
  bundled `tests/` use LVGL's own `lvgl/lvgl.h` include layout and break the
  Arduino build; we only need `src/` + `demos/`.

## Build

```bash
export PATH="$PATH:/c/Users/m/AppData/Local/arduino-cli/bin:/c/Users/m/bin"
cd esp32c6_480x480/firmware
B="esp32:esp32:esp32c6:FlashMode=qio,FlashSize=16M,PartitionScheme=app4M_fat8M_16MB,CDCOnBoot=cdc,CPUFreq=160"
INC="--build-property compiler.c.extra_flags=-I../../shared --build-property compiler.SS.extra_flags=-I../../shared --build-property compiler.cpp.extra_flags=-I../../shared"
arduino-cli compile -b "$B" --libraries lib --warnings none $INC .
```

Result: ~3.44 MB (82% of the 4 MB app partition — the 4 scene bitmaps in
`plumber_scene.h` dominate), 159 KB RAM (48%).

Board options that matter:
- `FlashSize=16M` + `PartitionScheme=app4M_fat8M_16MB` — 16 MB flash panel.
  The 4 scene pictures overflow the stock 3 MB app partition, so the build
  must use the 4 MB-app scheme (the scene pictures are the whole reason it's
  4 MB).
- `CDCOnBoot=cdc` — routes `Serial` (console) to USB-CDC, so the monitor works
  over the single on-chip USB port (COM3) with no extra UART chip.

## Flash

```bash
arduino-cli upload -p COM3 -b "$B" .
```
(`upload` does **not** take `--libraries` or `--warnings` — just `-p`/`-b`.)
COM3 = the Espressif on-chip USB-Serial-JTAG (VID 303A:1001). The C6 is in
normal (booted) mode and flashes straight through — no BOOT-button entry
needed for normal uploads.

## Verify (serial)

```bash
# heartbeats every 5s while running
arduino-cli monitor -p COM3 -c baudrate=115200
```
A clean reset shows: `ESP-ROM:esp32c6…` → `DeskBuddy boot: initializing PMIC +
display` → `Display up: static DeskBuddy card` → repeating
`heartbeat: static card alive`. If you only see `heartbeat` lines, that's the
already-running firmware (no boot banner) — toggle a reset to capture the boot
(`python scratch/reset_listen.py COM3 20`: DTR/RTS reset + 20s listen).

## LVGL v9 gotchas (learned the hard way)

- `lv_obj_set_text()` does **not** exist in v9 — use `lv_label_set_text()` on
  labels (there is no generic object text setter).
- The bundled 9.3.0 Montserrat only goes up to size **48** (no 56/64/72).
  Sizes to enable live in `lv_conf.h` — `LV_FONT_MONTSERRAT_<n> 1` — in BOTH
  copies (`lib/lvgl/lv_conf.h` and `lib/lvgl/src/lv_conf.h`), or the font
  symbol won't link. The static card uses 48/28/24/20.
- The bundled 9.3.0 Montserrat (basic-latin build) does **not** have `·`
  (U+00B7) or `—` (U+2014) — they render as empty boxes. Use ASCII
  separators (`-`, `:`) in on-panel strings; keep UTF-8 for widget-side text.
- The card mirrors the widget design: black AMOLED backdrop → rounded dark
  card (pad 28, radius 24) → green status dot + big number (accent teal)
  left → model + capacity right-aligned → "left" line → dim footer. Palette
  in `firmware.ino` matches `wiki/widget/modules/render.md`.
- **Two server rows** (widget default config): the model name at 28px is
  367px wide — it does NOT fit beside the big number, so each row stacks
  model / dot+number / capacity / unit+eta / footer vertically. Measure real
  widths from `lib/lvgl/src/font/lv_font_montserrat_*.c` (`adv_w` entries,
  /16.0) before placing anything — runtime `lv_text_get_size` also works.

## Screenshot capture (self-verify the panel)

The SH8601 is write-only — no hardware readback — but `firmware.ino` swaps
the LVGL flush callback at boot, forces a full-screen redraw, and streams
every flushed tile over serial (`TILE x y w h` + base64 RGB565 lines, until
`END`). The host composites the 480×480 PNG:

```bash
python esp32c6_480x480/firmware/tools/capture_panel.py COM3 panel.png   # needs pyserial + Pillow
```

The script does the DTR/RTS reset itself, so it can be run right after an
upload. This is how the assistant visually verifies layout changes on a
headless host — iterate: edit → compile → upload → capture → inspect.

Two live-capture variants (no reset, command-driven over USB-CDC):

- `capture_live.py COM3 out.png` — launches a generation, waits for the card
  to report `WORKING`, then sends `CAP`.
- `capture_scene.py COM3 out` — drives the [plumber scene](modules/scene.md)
  with the `SCN`/`SCP`/`SCI`/`SDN`/`CAR` serial commands and captures
  all five views (working / prompting / idle / down / card) to PNGs.

## Screen rotation (90° CW)

The UI is shown rotated 90° CW. LVGL's `lv_display_set_rotation()` is a
NO-OP for content in this build (LVGL 9.3 sw renderer never applies the
display rotation to tiles, even with `LV_USE_MATRIX` + `LV_DRAW_TRANSFORM_USE_MATRIX`
enabled), so the port does it itself in the flush (`bsp_lvgl_port.cpp`):

- `rot90_cw(src, dst, w, h)` — rotates an RGB565 tile 90° CW using the
  `BUFF_SIZE` scratch buffer (tiles are ≤ 480×50 px, both directions fit).
- `my_disp_flush` rotates the tile, then blits it at the physical position
  `px = H-1-ly, py = lx` (logical → physical for 90° CW).
- `cap_flush` in `firmware.ino` mirrors the same transform, so captures
  show exactly what the glass shows.

Keep the two in sync if you change the orientation.

## Next (M1 live)

Replace the hard-coded `ROW1`/`ROW2` data in `firmware.ino` with live
`BuddyState`: Wi-Fi (spec §9 creds), poll `GET /slots` per configured server
(`shared/API_ENDPOINTS.md`), derive the Phase (down/idle/prompting/generating),
and refresh the rows — same `lv_label_set_text()` + `bsp_lvgl_lock/unlock`
pattern. Keep the boot screenshot capture in place as a regression aid.

## Shared network config (WiFi creds)

WIFI credentials + LAN endpoint + weather API host live in **`shared/net.env`** (gitignored;
template `shared/net.env.example`). `python shared/tools/net_config.py`
generates **`shared/net_config.h`** (also gitignored) — all firmwares
`#include <net_config.h>` it (`WIFI_SSID` / `WIFI_PSWD` / `LAN_HOST` /
`LAN_PORT` / `WEATHER_HOST` / `WEATHER_LAT` / `WEATHER_LON` — the weather
macros are ready for the live-fetch firmware; the panel renderers still use
the verified Open-Meteo host). The compile must add the shared dir as an include path:

```
--build-property compiler.c.extra_flags=-I<repo>/shared \
--build-property compiler.SS.extra_flags=-I<repo>/shared \
--build-property compiler.cpp.extra_flags=-I<repo>/shared
```

(`arduino-cli compile` has no `--include` flag; the build-properties are
baked into the build cache — a config change needs a fresh compile, and the
properties must be passed to `compile`, not `upload`.)
