# shared/scene-assets — the DeskBuddy scene bitmaps (source of truth)

One scene, **"Plumber's run"** (an 80×80 pixel-art level), rendered once by
`render_scene.py` into bitmaps for **both** targets — widget and device. The
Rust widget and the ESP32 firmware no longer draw the scene procedurally;
they blit these pre-rendered pictures.

## The four pictures

| file (`bitmaps/<target>/<state>`) | state | content |
|---|---|---|
| `working`   | `Phase::Generating` | day level, sprinting plumber on the ground (no coins) |
| `prompting` | `Phase::Prompting`  | **same picture as `working`** (reused; no distinct art) |
| `idle`      | `Phase::Idle`       | the calm day level itself — **scene only, no plumber** |
| `down`      | `Phase::Down`       | calm level + a **Piranha Plant in the middle of the scene** (red spotted head, open fanged maw). No plumber, no coins. |

Notes on the direction this took:
- **The night scene was removed entirely** (it was a standalone picture that
  mapped to no `Phase`); there are exactly four states now.
- **Idle is scene-only**: the character appears only while the server is busy
  (working/prompting) only; idle is the calm level itself and down shows the
  Piranha Plant (see
  `state_down()` in `render_scene.py`).

## Layout

```
shared/scene-assets/
├── README.md                  <- this file
├── scene_design_sheet.png     (design sheet: 4 candidate scenes × day/night/device)
├── scene_design_sheet.js      (the HTML/JS generator that produced the sheet)
├── scene-mode.html            (standalone mockup of the widget's scene mode)
├── render_scene.py            THE generator — run it to regenerate everything
└── bitmaps/
    ├── widget/                320×320 (80×80 @ 4 px/cell)
    │   ├── <state>.png × 4    review / reference
    │   └── scene_assets.rs    AUTO-GENERATED — const u8 RGBA (A=0xFF), embedded by
    │                          the widget via #[path] (widget/src/main.rs)
    └── device/                480×480 (80×80 @ 6 px/cell — full-bleed square panel)
        ├── <state>.png × 4    review / reference
        ├── <state>.bin × 4    480×480 RGB565 LE (byte-identical to an lv_color_t buffer)
        └── (consumed by) esp32c6_480x480/firmware/plumber_scene.h
                               AUTO-GENERATED — four `scene_<state>_data` arrays
```

## Regenerate

```
python shared/scene-assets/render_scene.py     # needs Pillow
```

That rewrites `bitmaps/widget/*`, `bitmaps/device/*`, **and**
`esp32c6_480x480/firmware/plumber_scene.h`. The two generated files must be
committed together with any change to `render_scene.py`.

## How each target uses them

- **Widget** (`widget/src/main.rs`): `mod scene_assets`
  (`#[path = "../../shared/scene-assets/bitmaps/widget/scene_assets.rs"]`).
  `scene_images()` uploads the four day states via `ctx.load_texture` (egui
  0.36 `Painter::image` takes a `TextureId`); each scene band is a blit of
  the matching state. The bitmaps are **static** (the animation is gone —
  that's the point of bitmaps); the old 8 fps repaint cadence is kept for
  chrome responsiveness only.
- **Device** (`esp32c6_480x480/firmware/firmware.ino`): `plumber_scene.h`
  gives four `scene_<state>_data` RGB565 arrays in rodata (~1.8 MB total —
  480×480, **full-bleed square**: the whole panel is scene, sky at the top
  and ground at the bottom, no letterbox bands). They wrap into
  `lv_image_dsc_t`s on a dedicated scene screen (`build_scene()`), centered
  on the black `C_PANE` background. The picture **follows the live phase**
  (the poll loop swaps it while the scene is open); tapping only toggles
  card ↔ scene. The four pictures are too big for the stock 3 MB app
  partition, so the device is built and flashed with the custom
  `app4M_fat8M_16MB` partition scheme (4 MB app ×2 / 8 MB FAT; see
  `firmware/partitions/app4M_fat8M_16MB.csv` and the same-named entry in
  the esp32 core's `boards.txt`).
  Serial debug drives the same path: `SCN`/`SCP`/`SCI`/`SDN` then
  `CAP` (see `firmware/tools/capture_scene.py` — capture works only through
  LVGL, which is why the scene is an image, not a raw panel blit).
