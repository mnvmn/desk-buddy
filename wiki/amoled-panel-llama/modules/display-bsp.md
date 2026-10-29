# Module: `display_bsp` (device only)

> **Status: implemented** — `bsp_lvgl_port.{cpp,h}` in
> `amoled-panel-llama/firmware/` (Waveshare's LVGL v9 port, with the bundled
> `esp_lcd_sh8601` 4-bit driver in `src/externLib/`). Verified on the real
> panel: bring-up log `Display up: …` + working renders.

Wraps the Waveshare panel bring-up so `firmware.ino` never touches raw LCD
registers.

## Responsibilities

- Initialize the 480×480 RGB565 AMOLED via the **bundled `esp_lcd_sh8601`
  4-bit driver** — confirmed working on the real panel (the spec's open
  question about the unverified CO5300/QSPI controller is resolved: the
  bundled driver is the right one).
- Expose a minimal draw surface to LVGL v9 (the render target).
- AMOLED = no backlight (`GPIO_NUM_NC`); brightness is per-pixel / panel
  command.
- **`bsp_lvgl_lock/unlock`** — the lock around any code that mutates LVGL
  objects from outside the LVGL task (serial handler, poll loop). The flush
  callback streams each tile over USB-CDC during `CAP`.

## Pins (from the bundled `user_config.h`)

| Signal | GPIO |
|---|---|
| LCD CS | 15 |
| LCD PCLK | 0 |
| LCD DATA0–3 | 1, 2, 3, 4 |
| RST / DC / TE | NC / via driver |

## Gotchas

- **The panel is write-only** (no framebuffer readback). The LVGL draw buffers
  are the ground truth, so screenshots stream the flushed tiles over USB-CDC
  (`CAP`, or auto-capture at boot) — see [input.md](input.md).
- **The flush rotates every tile 90° CW** (`px=H-1-ly, py=lx`) — everything is
  authored in LVGL logical coordinates; do not pre-rotate assets.
- **TF card shares DATA0/DATA1/PCLK** with the LCD (`SD_MOSI=1, SD_MISO=2,
  SD_CLK=0, SD_CS=6`) — they cannot be active at once. The device ignores the
  TF slot; images are baked into flash (the custom `app4M_fat8M_16MB`
  partition scheme — see [firmware-build.md](../firmware-build.md)).

## Dependencies

- **Used by:** [ui](ui.md), [scene](scene.md) (render target + lock).
- **Uses:** `esp_lcd_sh8601` + LVGL v9 (from the bundled `09_LVGL_V9_Test`
  example), AXP2101 via `src/port_bsp/`.
