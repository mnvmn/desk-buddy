# Module: `button` (device only)

> **Status: spec only** — no firmware in `esp32c6_480x480/` yet.

The single user input for MVP: the **KEY** button.

## Responsibilities

- Debounce ≥ 10 ms; detect a single press.
- Toggle Live ⇄ Screensaver (B3) — ignored while in an error state.
- **Touch tap-anywhere is implemented** (CST9220, I2C SCL=7/SDA=8) — it drives
  the [scene](scene.md) tap-to-toggle today, via LVGL's pointer indev, not the
  KEY button.

## Gotchas

- **KEY GPIO is not exposed in the bundled Waveshare examples** — the one
  unconfirmed hardware fact. Read it off the schematic (or measure) before
  writing this module. Do not guess.
- **Do not use BOOT** for the toggle — it doubles as a download-mode pin
  and can fight PCLK.
- **Do not repurpose PWR** — it's managed by the AXP2101 PMIC (short = on,
  long = off), not a plain GPIO.

## Dependencies

- **Used by:** [ui](ui.md) (mode toggle)
- **Uses:** GPIO + debounce; [config](../../widget/modules/config.md) for nothing at MVP
