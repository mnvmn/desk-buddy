# Module: `display_bsp` (device only)

> **Status: spec only** — no firmware in `esp32c6_480x480/` yet.

Wraps the Waveshare panel bring-up so `ui` never touches raw LCD registers.

## Responsibilities

- Initialize the 480×480 RGB565 AMOLED via the **bundled `esp_lcd_sh8601`
  4-bit driver** — that is the source of truth, not the doc's unverified
  CO5300/QSPI claim.
- Expose a minimal draw surface to LVGL v9 (the render target).
- AMOLED = no backlight (`GPIO_NUM_NC`); brightness is per-pixel / panel
  command.

## Pins (from the bundled `user_config.h`)

| Signal | GPIO |
|---|---|
| LCD CS | 15 |
| LCD PCLK | 0 |
| LCD DATA0–3 | 1, 2, 3, 4 |
| RST / DC / TE | NC / via driver |

## Dependencies

- **Used by:** [ui](ui.md)
- **Uses:** Waveshare `sh8601` driver + LVGL v9 (start from the bundled
  `09_LVGL_V9_Test` example)

## Gotchas

- **TF card shares DATA0/DATA1/PCLK** with the LCD (`SD_MOSI=1, SD_MISO=2,
  SD_CLK=0, SD_CS=6`) — they cannot be active at once. MVP ignores the TF
  slot; images are baked into flash. Live image-swap is post-MVP and needs
  this pin-mux resolved first.
- Verify on the real panel during M1 bring-up that the `sh8601` driver is
  in fact the right one (open question #2 in the spec).
