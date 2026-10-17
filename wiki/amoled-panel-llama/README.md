# ESP32 Device (M1-live + scene done)

> Status: **live card + state-following scene on the panel.** The desk-buddy
> card (same layout as the widget's data-driven design: green status dot, big
> tok/s number, model + capacity, "left" line, dim footer) polls `/slots`
> (plus `/metrics` for Gufo servers) on two servers and renders live on the
> 480×480 AMOLED. The device's default screen is the **plumber scene**
> (pre-rendered pictures that follow the live state); a tap or `CAR` shows
> the card. Build/flash recipe:
> [firmware-build.md](firmware-build.md).
>
> Everything below comes from
> [`amoled-panel-llama/PRODUCT_SPEC.md`](../../amoled-panel-llama/PRODUCT_SPEC.md) (Waveshare
> ESP32-C6-Touch-AMOLED-2.16, `-EN` variant). The device mirrors the widget's
> [buddy-state](../desktop-llama/modules/buddy-state.md) semantics in C. Shared docs:
> [../shared/](../shared/); sibling e-paper device:
> [../epaper-panel-weather/](../epaper-panel-weather/).

## Milestones (per the product spec §11)

- **M0 — Bring-up (DONE):** Waveshare LVGL v9 demo flashed; panel renders,
  serial heartbeat alive. See [firmware-build.md](firmware-build.md).
- **M1-static — Card design (DONE):** the desk-buddy card, matching the
  widget's data-driven design, rendered with representative values — layout,
  palette, and fonts locked on the real panel.
- **M1-live — Connectivity (DONE):** Wi-Fi + `GET /slots` poller feeding the
  card; live tok/s on the panel while a generation runs.
- **M2 — Live screen (DONE):** tok/s + idle on the 480×480 AMOLED; per-row
  error states for wifi/server down.
- **M3 — Screensaver + input (DONE, diverged from spec):** the spec's KEY
  button was never built (GPIO was never confirmed); the input is **touch**
  (tap toggles scene ⇄ card) + the **serial command console** — see
  [modules/input.md](modules/input.md). The screensaver is the
  [plumber scene](modules/scene.md): four pre-rendered pictures following the
  live state, baked into flash, with serial picture overrides
  (`SCN`/`SCP`/`SCI`/`SDN`).
- **M4 — Polish (partial):** `/metrics` gauge path for Gufo servers (done —
  see below), CST9220 tap-anywhere (done). Still open: image swap without
  reflash (needs the LCD/SD pin-mux resolved), RTC clock, AMOLED blanking idle.

## Modules

- [modules/display-bsp.md](modules/display-bsp.md) — sh8601 4-bit panel
  bring-up wrapper, `bsp_lvgl_lock`, flush rotation (**implemented**)
- [modules/ui.md](modules/ui.md) — the data card + scene screens in
  `firmware.ino` (**implemented**)
- [modules/input.md](modules/input.md) — touch (CST9220) + serial command
  console (**implemented**)
- [modules/scene.md](modules/scene.md) — plumber state-following pictures
  (pre-rendered, flash-backed, **implemented + verified**)
- config (NVS): see [../desktop-llama/modules/config.md §Device](../desktop-llama/modules/config.md#device--nvs-spec-only)

## Gufo (`serve llm`) servers

A Gufo server's `/slots` is a **static** descriptor — `{id, task_id, state,
prompt, next_token, model}` — with no `is_processing` and no live decode
counter (the firmware detects it on the first slot lacking `is_processing` and
sets `s.gufo`). So there is no live rate to render, and the only activity
signal is the cumulative `tokens_predicted_total` counter, which advances
**only when a request completes**. On each `/metrics` feed the firmware runs
the same freshness rule as the widget: a counter advance sets `gufo_active`
and resets `gufo_idle`; a flat feed increments `gufo_idle`, and once it passes
`GUFO_ACTIVE_POLLS` (**4**, ≈ 8 s at the 2 s cadence) `gufo_active` falls back
to false. The render branches on it:

- **active** (`gufo_active` + gauge) → **green** dot, headline = the
  `predicted_tokens_seconds` gauge (the last completed request's tok/s),
  leftover line `just generated`.
- **settled** (gauge present, not active) → **grey** dot, headline = the gauge
  value, unit `tok/s last`, leftover line `last request`.
- **no gauge yet** → grey dot, headline `-`, leftover line `ready`.

The `avg N` session chip is **skipped** on a Gufo row (the gauge *is* the
headline, so a second "avg" chip would just repeat it). Parity with the
widget: same counter-advance rule and `GUFO_ACTIVE_POLLS = 4` — see
[buddy-state](../desktop-llama/modules/buddy-state.md) and
[parity](../shared/parity/README.md).

## Open hardware questions

- **TF card vs LCD pins** — SD shares DATA0/DATA1/PCLK with the LCD; they
  cannot be active at once. Images are baked into flash; live swap is blocked
  on this pin-mux.

Resolved: the **KEY button GPIO** question is moot — the button was never
built (touch + serial are the inputs, see
[modules/input.md](modules/input.md)). The **`sh8601` driver** claim is
confirmed on the real panel (bundled 4-bit driver works). **`--metrics`**
resolved 2026-09-14: on.
