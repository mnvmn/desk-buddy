# Module: `input` (device only)

> **Status: implemented.** There is **no physical button** in the firmware —
> the spec's KEY button (M3) was never built. The two real inputs are the
> **CST9220 touch** (LVGL pointer indev) and the **serial command console**
> (USB-CDC). Both are handled in `loop()` in `firmware.ino`.

## Touch

- CST9220 over I2C (SCL=7 / SDA=8), surfaced to LVGL as the pointer indev.
- Tapping toggles **scene ⇄ card**: a tap on the [scene](scene.md) opens the
  data card; a tap on the card re-opens the scene showing the *currently
  derived* picture. It is the device's only hands-on input and the normal way
  to flip screens.
- No custom tap state machine — LVGL's indev handles it; the tap is a plain
  full-screen `LV_EVENT_CLICKED` catcher (see [scene.md §Touch](scene.md#touch)).

## Serial command console (3-char tokens)

Typed on the USB-CDC serial console; matched on a newline, executed under the
LVGL lock where they mutate the screen. This is the primary verification
channel (the SH8601 panel is write-only, so serial + `CAP` screenshot is how
you inspect the render):

| cmd | effect |
|---|---|
| `CAR` | show the data card (clears any held scene override) |
| `SOP` | open the scene exactly like a card tap; resets to live-follow |
| `SCN` | scene → **working** picture (manual override, held) |
| `SCP` | scene → **prompting** picture (manual override, held) |
| `SCI` | scene → **idle** picture (manual override, held) |
| `SDN` | scene → **down** picture (manual override, held) |
| `CAP` | re-stream the current framebuffer over USB-CDC (screenshot) |

`SCN/SCP/SCI/SDN` are **manual overrides**: each opens the scene if closed and
*holds* its picture — live re-derivation is suspended while an override is
set, so the panel can't fight the command back to IDLE. The override clears on
`CAR`, on a scene tap, or on `SOP`. `CAP` (and the boot-time auto-capture)
hold the LVGL lock while streaming, so a swap issued mid-capture paints
nothing. See [scene.md §Verification](scene.md#verification-no-physical-tapping).

## Hardware pin caveats

- **Do not use BOOT** for a future button toggle — it doubles as a
  download-mode pin and can fight PCLK.
- **Do not repurpose PWR** — it's managed by the AXP2101 PMIC (short = on,
  long = off), not a plain GPIO.

## Dependencies

- **Used by:** [scene](scene.md) (tap-to-toggle), the card screen (`CAR`).
- **Uses:** CST9220 I2C + LVGL pointer indev; the `bsp_lvgl_lock` guard from
  [display-bsp](display-bsp.md).
