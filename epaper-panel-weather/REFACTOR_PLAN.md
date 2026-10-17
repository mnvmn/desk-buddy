# Firmware refactor — split `weather_test.ino` into modules

**Status:** PLAN (not started) · **Owner:** desk-buddy · **Target dir:** `fw/weather_test/`

## Goal

Break the 1055-line monolith `fw/weather_test/weather_test.ino` into small,
single-responsibility files so the upcoming **v2 UI port** (2-day strip, moon
phase, rain state, new row glyphs) lands on a codebase that is easy to change
and review. **The refactor is behavior-neutral:** the same binary behavior,
same panel output. The v2 UI work happens *after* this, on the small files.

## Why now

- One file holds font + drawing + icons + network + JSON parsing + fetch +
  layout + orchestration + serial. Every v2 change means scrolling a 1055-line
  file and risking an unrelated break.
- The v2 port adds ~300 lines (new glyphs are already in; the UI, 2-day fetch,
  moon phase, rain state still need adding). Doing that inside the monolith is
  where mistakes happen.

## Baseline (committed, clean)

- `HEAD` == working tree, `weather_test.ino` = **1055 lines**, no pending diff.
- **Already in this baseline (committed, unused by UI yet):** glyphs 40–45
  (`~ ° > _ m s`) in `GLYPHS[]`/`glyph_idx()`; extended `WX` struct
  (`winds[13]`, `now_year/month/day`, `WXDay days[2]`). These are v2
  pre-work and are **kept as-is** by the refactor (moved verbatim).
- **Still 1.0 (unchanged by refactor, done in the later v2 task):** `draw_ui()`
  (bottom badges), `fetch_weather`/`fetch_sunset` (no 2-day fetch), no moon
  phase, no rain-state classification.

Acceptance for THIS refactor = compiles + flashes + healthy boot log + panel
looks like today. It is *not* the v2 UI.

## Codebase assessment (what gets split)

| Part | Verdict | Why |
| --- | --- | --- |
| `fw/weather_test/weather_test.ino` (1055 ln) | **SPLIT** (this plan) | the only true monolith |
| `lib/EDP/*` (EPD.cpp, EPD_Init.cpp, spi.cpp, …) | leave | already modular, vendor driver |
| `fw/weather_test/{net_config.h, le_dercainfo.h, statusimg.h}` | leave | config / generated bitmap / generated certs |
| `fw/test_sketch/` | leave | throwaway bring-up (output target of `gen_le_dercainfo.py` + the render tools) |
| `fw/certdump/` | **deleted 2026-10-17** | throwaway cert diagnostic, no longer referenced |
| `tools/*.py` (each ≤80 ln, one purpose) | leave | already one-file-per-purpose |
| `web/panel_v2.html` + `web/panel_v2/` | done (separate web split, 2026-10-17) | split into canvas/font/icons/ui/data/app; `panel_live`/`panel_annotated` removed |

So the action is firmware-only; the rest is assessed and left alone.

## Target structure

New files in `fw/weather_test/` (prefix `panel_`):

| Module | File(s) | Owns | ~ln |
| --- | --- | --- | --- |
| canvas | `panel_canvas.h` (header-only) | `INK`/`PAPER`, `cv_set()`, layout consts, `extern ImageBW`, pins, refresh timing | ~30 |
| font | `panel_font.h` (header-only) | `GLYPHS[]`, `glyph_idx()`, `put_text()`, `text_w()` | ~95 |
| draw | `panel_draw.h` (header-only) | `fill_circle`, `thick_line`, `fill_rect`, `round_rect_fill`, `icon_sun/moon/cloud/part` | ~95 |
| net | `panel_net.h` / `panel_net.cpp` | `le_rng`, `http_get` (raw mbedTLS) | ~90 |
| data | `panel_data.h` (header-only) | `WXDay`, `WX` structs; `extern wx` | ~35 |
| fetch | `panel_fetch.h` / `panel_fetch.cpp` | `fetch_weather`, `fetch_sunset`, `fetch_time`, `num_after`, `parse_num_array`, `parse_hhmm`, `fmt_num`, `is_daytime`, `slot_range`, `current_refresh_ms` | ~280 |
| state | `panel_state.h` / `panel_state.cpp` | globals (`wx`,`cur_city`,`wifi_ok`,`lastTick`,`ImageBW`), `WxCity`+`CITIES[]`+`N_CITIES`, `wifi_up`,`ensure_wifi`,`refresh_data`,`do_full_refresh`,`render_and_show`,`toggle_city`, `cap_b64`,`do_capture` | ~190 |
| ui | `panel_ui.h` / `panel_ui.cpp` | `draw_temp_curve`, `draw_ui` (+ later `draw_two_day_strip`, moon phase, small icons) | ~280 |
| main | `weather_test.ino` | `poll_serial`, `setup()`, `loop()` (buttons + tick) — control flow only | ~150 |

**Shape:** 3 header-only (canvas/font/draw) + 4 `.h/.cpp` pairs (net/fetch/
state/ui) + the thin `.ino`. Every file ≤ ~280 lines; the `.ino` drops
1055 → ~150.

## Section → file map (exact current line ranges)

| Current `weather_test.ino` lines | → new home |
| --- | --- |
| 23–35 includes | split across module headers; `.ino` includes the module headers |
| 37–44 pins + `REFRESH_MS*` | `panel_canvas.h` |
| 46 `ImageBW` buffer | `panel_state.cpp` (def) + `panel_canvas.h` (extern) |
| 50 `struct WxCity;` fwd decl | drop (defined in `panel_state.h`) |
| 55–56 `INK`/`PAPER` | `panel_canvas.h` |
| 58–67 layout consts | `panel_canvas.h` |
| 69–119 `GLYPHS[]` | `panel_font.h` |
| 120–142 `glyph_idx()` | `panel_font.h` |
| 144–153 `cv_set()` | `panel_canvas.h` |
| 155–173 `put_text()` | `panel_font.h` |
| 174 `text_w()` | `panel_font.h` |
| 176–181 `fill_circle` | `panel_draw.h` |
| 184–195 `thick_line` | `panel_draw.h` |
| 197–201 `fill_rect` | `panel_draw.h` |
| 204–220 `round_rect_fill` | `panel_draw.h` |
| 223–238 `icon_sun` | `panel_draw.h` |
| 243–246 `icon_moon` | `panel_draw.h` |
| 248–254 `icon_cloud` | `panel_draw.h` |
| 256–273 `icon_part` | `panel_draw.h` |
| 277–282 `WxCity`, `CITIES[]`, `N_CITIES` | `panel_state.h` (def) |
| 284–286 `cur_city`,`wifi_ok`,`lastTick` | `panel_state.cpp` (def) + `panel_state.h` (extern) |
| 290–297 `WXDay` | `panel_data.h` |
| 298–313 `WX` | `panel_data.h` |
| 314 `WX wx` global | `panel_state.cpp` (def) + `panel_data.h` (extern) |
| 319–322 `current_refresh_ms` | `panel_fetch` |
| 328–340 `is_daytime` | `panel_fetch` |
| 346–349 `slot_range` | `panel_fetch` |
| 352–367 `fmt_num` | `panel_fetch` |
| 370–372 `le_rng` | `panel_net` |
| 375–451 `http_get` | `panel_net` |
| 453–462 `num_after` | `panel_fetch` |
| 464–482 `parse_num_array` | `panel_fetch` |
| 484–550 `fetch_weather` | `panel_fetch` |
| 552–561 `parse_hhmm` | `panel_fetch` |
| 563–580 `fetch_sunset` | `panel_fetch` |
| 582–586 `fetch_time` | `panel_fetch` |
| 589–748 `draw_temp_curve` | `panel_ui` |
| 750–864 `draw_ui` | `panel_ui` |
| 867–880 `cap_b64` | `panel_state` |
| 882–886 `do_capture` | `panel_state` |
| 889–899 `wifi_up` | `panel_state` |
| 901–904 `ensure_wifi` | `panel_state` |
| 907–913 `refresh_data` | `panel_state` |
| 916–921 `do_full_refresh` | `panel_state` |
| 924–934 `render_and_show` | `panel_state` |
| 938–944 `toggle_city` | `panel_state` |
| 949–983 `poll_serial` | `weather_test.ino` |
| 985–1028 `setup()` | `weather_test.ino` |
| 1030–1055 `loop()` | `weather_test.ino` |

Move code **verbatim** (only change: `static` → external linkage + add
declarations to the matching `.h`). Do not retype logic.

## Dependency graph (no cycles)

```
net_config.h / le_dercainfo.h / EPD.h        (existing, external)
        |
   panel_canvas.h   (INK/PAPER, cv_set, layout, ImageBW extern, pins)
   panel_data.h     (WX, WXDay, extern wx)
   panel_net.cpp    (http_get, le_rng)        <- net_config, certs, mbedtls
   panel_font.h     <- panel_canvas.h         (put_text calls cv_set)
   panel_draw.h     <- panel_canvas.h, math   (icons call primitives)
        |
   panel_fetch.cpp  <- panel_canvas, panel_data, panel_net   (reads wx)
        |
   panel_ui.cpp     <- canvas, font, draw, data, fetch, state (reads wx, CITIES)
        |
   panel_state.cpp  <- data, net, fetch, ui   (defines wx/CITIES; render_and_show->draw_ui)
        |
   weather_test.ino <- state, ui, fetch, canvas   (setup/loop/poll_serial)
```

**The state↔ui link is safe:** `panel_state.h` does *not* include
`panel_ui.h` (it only *declares* `render_and_show`); `panel_ui.cpp` includes
`panel_state.h` for the `wx`/`CITIES` globals, and `panel_state.cpp` includes
`panel_ui.h` to call `draw_ui`. Headers never cycle.

## Arduino / build gotchas

1. **`.ino` auto-prototype is `.ino`-only.** Functions in sibling `.cpp` are
   NOT auto-declared — every cross-module function needs a `.h` declaration
   (the map above already assigns each to a header).
2. **`setup()`/`loop()` stay in `weather_test.ino`.** Arduino expects them
   there; keep all control flow (buttons, tick, serial) in the `.ino`.
3. **`extern` globals.** `wx`, `cur_city`, `wifi_ok`, `lastTick` defined once in
   `panel_state.cpp`, declared `extern` in their headers. `panel_ui.cpp` and
   the `.ino` include `panel_state.h` to see them.
4. **`ImageBW` (34000 B) defined once** in `panel_state.cpp`, `extern` in
   `panel_canvas.h`. Only `panel_state` touches it (`Paint_NewImage`,
   `EPD_Display`, `cap_b64`).
5. **`static` → external linkage.** Drop `static` from functions that cross
   modules and add the matching `.h` prototype. `GLYPHS[]` stays `static const`
   in `panel_font.h` (const data, one copy per including TU — negligible).
6. **EDP lib is on the include path** via the build's `--library
   "$ROOT/lib/EDP"`, so `#include "EPD.h"` works in any `.cpp`. `mbedtls/*`
   and `esp_random.h` come from the ESP32 core; `net_config.h` from
   `-I$REPO/shared`; `le_dercainfo.h` from the sketch dir. Each `.cpp` includes
   only what it uses.
7. **`.cpp` in the sketch folder is supported** by `arduino-cli compile` — it
   compiles all sibling `.cpp` files alongside the `.ino`.
8. **`math.h`** needed by `round_rect_fill`/`thick_line` (canvas/draw) and the
   umbrella `cosf/sinf` (ui).

## Phased execution (compile after each)

- **P0 — golden.** Capture the current panel via `CAP` → decode base64 →
  `fw/weather_test/golden_before.png`. (Live-data caveat: see Verification.)
- **P1 — canvas/font/draw.** Create the 3 header-only files; delete the moved
  ranges from the `.ino`; add `#include`s. **Compile.**
- **P2 — net.** `panel_net.{h,cpp}`. **Compile.**
- **P3 — data/fetch.** `panel_data.h` + `panel_fetch.{h,cpp}`. **Compile.**
- **P4 — ui.** `panel_ui.{h,cpp}` (`draw_temp_curve`, `draw_ui`). **Compile.**
- **P5 — state + thin `.ino`.** `panel_state.{h,cpp}`; slim the `.ino` to
  `poll_serial`+`setup`+`loop`. **Compile.**
- **P6 — deploy + verify.** Flash to COM4, read boot log, `CAP` →
  `golden_after.png`, visual compare. Commit.

Each phase leaves a compiling tree, so any break isolates to one module move.

## Verification

- **Hard gates:** clean `arduino-cli compile` (README command), successful
  flash, healthy boot log (`wx: OK …`, `TLS verified`, `panel up: … live`),
  no `font: missing glyph` lines.
- **Golden CAP:** pixel diff `golden_before.png` vs `golden_after.png`.
  Caveat: the panel shows *live* weather, so two captures at different times
  will differ in the *numbers* — the **layout** must be identical. To make the
  diff rigorous, re-capture with the same data window: send the same `MOCK
  hh:mm` command after both flashes and compare; or compare structurally
  (same icons/positions, only digits differ). Because the move is verbatim
  (no logic retyped), compile + log + visual-identical layout is the real gate.

## Out of scope

- The v2 UI itself (2-day strip, moon phase, rain state, small row icons,
  `fetch_two_days`) — separate task, done **after** this, landing in
  `panel_ui.cpp` / `panel_fetch.cpp` / `panel_font.h`. See
  [`V2_UI_PORT_PLAN.md`](V2_UI_PORT_PLAN.md) for the full feature list.
- EDP vendor lib, `tools/*.py`, `web/*.html` (assessed above: already fine).
- Any behavior change, layout change, or new feature.

## After the refactor (where v2 lands)

- New row glyphs already in `panel_font.h` (`GLYPHS` 40–45).
- `fetch_two_days` + date/rain parse → `panel_fetch.cpp` (writes `wx.days[2]`).
- `draw_two_day_strip`, `moon_phase`/`draw_moon_phase`, `rain_state`, small
  icons → `panel_ui.cpp`.
- `draw_ui()` restructured to the v2 left column + strip.

Full feature list, dependency order, and build sequence for this v2 work:
[`V2_UI_PORT_PLAN.md`](V2_UI_PORT_PLAN.md).
