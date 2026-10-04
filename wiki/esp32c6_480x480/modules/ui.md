# Module: `ui` (device only)

> **Status: spec only** — no firmware in `esp32c6_480x480/` yet.

LVGL v9 screens on the 480×480 AMOLED. Renders [buddy_state](../../widget/modules/buddy-state.md)
only on change — no full-panel refreshes every frame.

## Responsibilities

- **Live — working:** large number (~120 px) + `tok/s` sub-label + optional
  `N left` line (when `n_remain >= 0`) + busy dot / progress arc.
- **Live — idle:** `—` / `idle`, dimmed so the desk reads "asleep."
- **Screensaver / scene:** full 480×480 plumber picture while working, a
  calmed one while not working — **the device's default screen**, with
  tap-to-toggle to the data card; see [scene](scene.md). Images are RGB565
  (~460 KB each), baked into flash.
- **Error states** (win over mode): `OFFLINE_WIFI` → wifi icon + `no link`;
  `OFFLINE_SERVER` → `--` + `server down`.
- **Boot:** ~300 ms "DeskBuddy" splash → straight to the scene (graphic).

## Mode handling

- `mode` (LIVE | SCREENSAVER) is set by [button](button.md); server state
  chooses the *variant* within the mode. A button press during an error
  does nothing.
- On returning to `OK`, refresh immediately and restore the pre-error mode.

## Dependencies

- **Used by:** [button](button.md) (toggles its mode)
- **Uses:** [buddy_state](../../widget/modules/buddy-state.md) (source of truth),
  [display_bsp](display-bsp.md) (render target), [config](../../widget/modules/config.md)
  (screensaver images)
