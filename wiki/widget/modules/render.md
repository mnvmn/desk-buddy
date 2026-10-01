# Module: `render` (widget only)

The visual layer. **Implemented at M2** as
[`widget/src/main.rs`](../../../widget/src/main.rs) (`BuddyApp`): a standard
`eframe`/`egui` 0.36 app showing the approved data-driven card — a dark,
translucent, borderless floating card with **one data row per configured
server, stacked vertically** (status dot + big number on the left, model +
capacity right-aligned, leftover line, tok/s sparkline + session footnote,
dim label/host footer per row). M3 is in: the top **drag strip** (drag-to-move)
+ top-right **close ×** (right-click menu), **click-on-content mode toggle**
(no dedicated button — a click on the card flips data ⇄ scene),
**multi-server vertical scale** (height grows with the config), and
**both display modes** (data card + the combined-activity "Plumber's run"
scene, `src/main.rs` → `scene_assets` bitmaps in
`shared/scene-assets/`). Compact, tray and position persistence remain.

## M1 (implemented) — `main.rs`

- `BuddyApp::new` creates `Arc<Mutex<WidgetState>>` (`buddy_state::SharedState`),
  loads the **config file** (`config::load` — server list + poll cadence,
  `%APPDATA%/DeskBuddy/widget.json`) and calls `poller::spawn_poller(...)`
  **before the first frame**, so data is flowing as soon as the window shows.
- `eframe::App::ui` each frame: locks + **clones** `WidgetState` (copy out of
  the lock so the UI never holds it while drawing).
- The window inner size is computed from the config: width fixed 320, height
  = `34 (drag strip) + n·176 (rows) + (n−1)·10 (gaps) + 14 (bottom pad)` —
  the card **scales vertically** with the server count.
- Content chosen by `WidgetState::display_mode` (data card vs scene) and,
  per row, by each `BuddyState::phase()` — see M2 below.
- Right-click anywhere on the card opens a menu (switch mode / Quit); Quit
  calls `ctx.send_viewport_cmd(ViewportCommand::Close)`.

### eframe/egui 0.36 API (this is what the code uses — not the old tutorials)

- The `App` trait method is **`fn ui(&mut self, ui: &mut egui::Ui,
  frame: &mut eframe::Frame)`** (not `update`).
- Panels are **`egui::Panel::top(id)` / `egui::CentralPanel::default()`**
  (not `TopBottomPanel`).
- Quit via **`ctx.send_viewport_cmd(ViewportCommand::Close)`** (not
  `ctx.close()`).
- Themed: **`ctx.set_theme(ThemePreference::Dark)`** + `ctx.set_visuals(…)`;
  `set_visuals` targets the *active* theme, so pin the theme first.
- Popups: **`response.context_menu(|ui| …)`**; close with **`ui.close()`**
  (there is no `close_menu`).
- `painter.text(pos, align, …)` returns the drawn `Rect` (0.36 has no
  `text_with_layout`); `painter.rect` takes a `StrokeKind`;
  `Rect::shrink2` takes a `Vec2`, not a `Margin`.

## M2 (implemented) — the data-driven card

The card is a **dark translucent rounded rect painted with the painter** into
a **transparent, undecorated, always-on-top** window — no window chrome, no
egui theme background. It holds **one row per server** (from the config),
stacked top→bottom in config order with a thin rule between rows. Each row's
lines are built by helper methods on `BuddyApp` from that server's cloned
`BuddyState`, chosen by `BuddyState::phase()`:

- `headline()` → `(big, sublabel, dot_color)` per phase:
  - `Generating` → `{toks_per_s:.1}` / `tok/s`, dot green `#4ec978`, number
    in accent `#4ec9b0`.
  - `Prompting` → `···` / `prompting…`, number + dot amber `#e6b450`.
  - `Idle` → `—` / `idle`, gray dot `#7a808a`.
  - `Down` → `--` / `server down`, red `#e05656` (+ red-tinted card border).
- Model line / `capacity_line()` → the **right-aligned** block:
  `Qwen3.8-27B-UD-Q4_K_XL` over a muted capacity line; hidden until the model
  resolves. The model name is the **basename** of
  `model_alias` with `.gguf` stripped (the server reports a full path). The
  model line is **ellipsized front-first** (`fit_text`) and anchored to
  `close.left() − 8 px` so long live names never run under the corner chrome.
  The capacity line shows **live context occupancy** when `/slots` delivered
  `n_prompt_tokens` + a window (`/props` `n_ctx`, slot `n_ctx` backs up):
  `72k/160k · 45% · Q4_K - Medium` — the `72k/160k · 45%` part turns
  **amber** once usage passes **80%** (filling context visible at a glance);
  otherwise the whole line stays muted. No occupancy yet → the static
  `160k ctx · Q4_K - Medium`.
- `remaining_line()` → `~{eta:.0}s left · {n_remain} tok` when bounded **and** a
  rate exists, else `{n_remain} tok left`; the other phases get a fixed line
  (`processing prompt` / `loaded · ready` / `retrying in 5 s` red).
- Sparkline + `footnote()` → `avg {session_avg_tps:.0} · {kv_reuse}% cached`
  with a bar sparkline of the last ≤10 live tok/s samples
  (`BuddyState::rate_history`, fed by `feed_ok`). Hidden until `/metrics`
  delivered data — **and hidden entirely when `Down`** (`feed_error` clears
  `session_avg_tps`, `kv_reuse` and the history: no stale stats in the down
  state).
- Footer: dim `{label} · {host}` per row (label from the config, 9.5 pt).

**The scene display mode** (`WidgetState::display_mode == Scene`) swaps the
whole card for **`scene::paint_scene_combined`**: one full day-level **band
per server** (the 80×48 "Plumber's run" grid scaled to the band rect), stacked
in config order, each in that server's own phase (sprinting / same picture as
working / calm scene only / piranha plant), with a status dot +
label beside each band. The character appears while busy (working /
prompting); down shows the Piranha Plant instead; idle is scenery only. The bands share one palette and
sky, so the fleet reads as one world. All day — there is no night variant.

**Palette:** the whole theme is a fixed dark card (the card renders
identically in every system theme): bg `rgba(30,30,34,0.82)`, border
`rgba(255,255,255,0.11)` (→ red `rgba(224,86,86,0.4)` when down), ink
`#f2f3f5`, muted `#9aa0aa`, dim `#5f656f`, accent `#4ec9b0`.

**Layout (gotchas found via the screenshot tests — do not "simplify" these
back):**
- **No egui widget layout at all inside the card** — every element is
  `painter.text`/`painter.circle_filled`/`painter.rect_filled` at computed
  positions (a top-down `y` cursor). `vertical_centered_justified` stretches
  ~40 pt gaps between rows; `horizontal_centered` balloons a row from ~63 pt
  to ~156 pt; nested `with_layout(RightToLeft,…)` inside a centered column
  eats the remaining area. All three push rows off-canvas.
- The card is painted over a **transparent** window: the window is
  `with_transparent(true)` + `with_decorations(false)` + `with_always_on_top()`,
  the theme is pinned Dark, and the `CentralPanel` gets **`Frame::NONE`** —
  without it the *active theme's* `window_fill` paints the rounded corners
  (light `#f8f8f8` on a light-system host, dark `#1b1b1b` on dark), hiding
  the true transparency.
- The dot is **painted** (`painter().circle_filled`), not a text glyph:
  every round-bullet character is a *missing glyph* in egui 0.36's default
  font, and missing glyphs reserve a giant row height.
- All text is monospace (`FontFamily::Monospace`) at fixed sizes (42/13/12/
  10.5/9.5 pt), content rect = `viewport_rect().shrink(18)`, row gap 6.

**Screenshot-test mode:** with `DESKBUDDY_SCENE=generating|prompting|idle|down`
set, `BuddyApp::new` seeds a fully-deterministic `WidgetState` (one
`BuddyState` per configured server, `scene_for`, including a fixed sparkline
profile) and skips the poller; `main` pins the pixel ratio to 2.0. The
`__screenshot` hook fires on the 2nd pass, so the app requests one repaint
while capturing. See [getting-started.md §Screenshot tests](../getting-started.md).

Window: title `DeskBuddy` (undecorated), width 320, **height dynamic** =
`34 + n·176 + (n−1)·10 + 14` (two servers → 410 logical → 640×820 @2x golden).

## M3 (partly implemented) — the interaction layer

The card (chrome + content) and the **multi-server vertical scale** are done;
what's left is the rest of interaction:

- **Drag to move** (done: top drag strip via `ViewportCommand::StartDrag` →
  winit `drag_window()`).
- **Double-click Full ⇄ Compact** (compact ≈ 96×96, number only) — todo.
- **Right-click menu**: expand to Full/Compact, Pin on top, Hide, Quit.
  Hide-to-tray keeps the poller running. — todo.
- **Persistence:** window position + size mode (see [config](config.md)). The
  server list + poll cadence already come from the config file.
- **Single instance:** named mutex / lockfile — a second launch no-ops. — todo.
- HiDPI: egui scales automatically; keep logical-px layout (crisp at 125/150 %).

## Dependencies

- **Used by:** — (it's the leaf for the widget)
- **Uses:** [buddy_state](buddy-state.md) (what to show), [poller](poller.md)
  (background thread), `eframe`/`egui` 0.36.

## Later (M4)

- Native Win11 Mica/blur behind the card (egui has no Mica — needs a
  compositing hook or a pre-rendered background sampled from the desktop).
