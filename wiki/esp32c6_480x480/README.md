# ESP32 Device (M1-static done)

> Status: **M1-static — the real card is on the panel.** The static desk-buddy
> card (same layout as the widget's data-driven design: green status dot, big
> tok/s number, model + capacity, "left" line, dim footer) flashes and renders
> on the 480×480 AMOLED (`Display up: static DeskBuddy card` in serial). Values
> are hard-coded for now; the live `/slots` poller is next. Build/flash recipe:
> [firmware-build.md](firmware-build.md).
>
> Everything below comes from
> [`esp32c6_480x480/PRODUCT_SPEC.md`](../../esp32c6_480x480/PRODUCT_SPEC.md) (Waveshare
> ESP32-C6-Touch-AMOLED-2.16, `-EN` variant). The device mirrors the widget's
> [buddy-state](../widget/modules/buddy-state.md) semantics in C. Shared docs:
> [../shared/](../shared/); sibling e-paper device:
> [../esp32s3_272x792/](../esp32s3_272x792/).

## Milestones (per the product spec §11)

- **M0 — Bring-up (DONE):** Waveshare LVGL v9 demo flashed; panel renders,
  serial heartbeat alive. See [firmware-build.md](firmware-build.md).
- **M1-static — Card design (DONE):** the desk-buddy card, matching the
  widget's data-driven design, rendered with representative values — layout,
  palette, and fonts locked on the real panel.
- **M1-live — Skeleton (proves connectivity):** Wi-Fi + `GET /slots` poller
  feeding the card; `working/tok-s` live on the panel. Exit: serial shows live
  tok/s while a chat generates.
- **M2 — Live screen:** tok/s + idle on the 480×480 AMOLED; error states for
  server/Wi-Fi down. Exit: the desk display shows a correct live number and
  degrades gracefully when the server is killed.
- **M3 — Screensaver + button:** KEY toggles Live ⇄ Screensaver
  (imgA working / imgB idle, baked into flash); `img2buddy` ships two
  default images. Exit: full MVP behavior.
- **M4 — Polish:** optional `/metrics` gauge path, CST9220 touch tap-anywhere,
  image swap without reflash (needs the LCD/SD pin-mux resolved), RTC clock,
  AMOLED blanking idle.

## Modules (planned)

- [modules/display-bsp.md](modules/display-bsp.md) — Waveshare sh8601 4-bit
  panel bring-up wrapper (pins, AMOLED = no backlight)
- [modules/ui.md](modules/ui.md) — LVGL v9 screens: live, idle, screensaver,
  error, boot
- [modules/button.md](modules/button.md) — KEY debounce + Live ⇄ Screensaver
  toggle (GPIO **still unverified** — read the schematic first)
- [modules/scene.md](modules/scene.md) — plumber tap-to-toggle pictures
  (pre-rendered working / not-working, flash-backed, **implemented + verified**)
- config (NVS): see [../widget/modules/config.md §Device](../widget/modules/config.md#device--nvs-spec-only)

## Open hardware questions

- **KEY button GPIO** — not in the bundled examples; confirm from the
  schematic before writing `button`.
- **`sh8601` driver claim** — the doc mentions an unverified CO5300/QSPI
  controller; the bundled 4-bit driver is the working assumption. Verify on
  the real panel at M1 bring-up.
- **TF card vs LCD pins** — SD shares DATA0/DATA1/PCLK with the LCD; they
  cannot be active at once. MVP bakes images into flash.

Full open-questions list (spec §13): the KEY GPIO, the display-driver
confirmation, and `--metrics` (resolved 2026-09-14: on).
