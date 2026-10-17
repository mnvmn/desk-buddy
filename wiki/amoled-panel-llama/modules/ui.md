# Module: `ui` (device only)

> **Status: implemented** — the LVGL screens live in `firmware.ino` (the
> sketch is a monolith; there is no separate `ui` module — that split was
> proposed in the spec §10 but never made). Two screens: the **plumber
> scene** (device default, see [scene](scene.md)) and the **data card**
> (one row per server, mirroring the widget's
> [buddy_state](../../desktop-llama/modules/buddy-state.md) render).

## The data card

- One data row per server (two: `m.tower1:11444`, `m.strix:11444` via
  `LAN_HOST`/`LAN2_HOST` in `net_config.h`): status dot, big tok/s number,
  model + capacity right-aligned, "left" line, dim `label · host:port`
  footer — same layout rules as the widget (parity).
- **Working:** live `Δn_decoded / Δt` tok/s; **idle:** `—` / `idle`;
  **Gufo servers** render the `/metrics` gauge with the counter-advance
  freshness rule (see [README §Gufo](../README.md#gufo-serve-llm-servers)).
- **Error states** (per row): `OFFLINE_WIFI` → wifi down, `OFFLINE_SERVER` →
  server down. Render is on change — labels are updated in place, LVGL
  paints the dirty area.

## Screen switching

- Boot lands on the **scene** (`setup()` loads it after building both
  screens); a tap or `SOP` opens the scene, `CAR` / scene-tap shows the card.
  The live poll keeps the card's data fresh regardless of the active screen
  (it writes to the card's labels; LVGL paints them when the card is active).
- Poll cadence (parity P4): 500 ms while any server is working, 2 s idle,
  5 s while all disconnected.

## Dependencies

- **Used by:** [input](input.md) (touch + `CAR`/`SOP` switch screens).
- **Uses:** [display-bsp](display-bsp.md) (render target + lock),
  [scene](scene.md) (the default screen), `net_config.h` (server hosts),
  the per-server state structs fed by the `/slots` + `/metrics` pollers.
