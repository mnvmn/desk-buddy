# DeskBuddy code-quality alignment & fixes

## Goal
Make the `widget/` Rust source and its wiki docs consistent, intuitively named, and free of stray artifacts — without changing any rendered behavior (the golden-screenshot test stays green throughout).

## Current context / assumptions
Verified by direct inspection (do not re-derive):
- The Rust widget is a **binary-only** cargo package named `deskbuddy_widget` (`widget/Cargo.toml` has `[[bin]] path = "src/main.rs"`, **no** `[lib]`, **no** `src/lib.rs`). All four modules are declared in `src/main.rs`: `mod buddy_state; mod config; mod poller;` (+ generated `mod scene_assets;`).
- Source files: `widget/src/buddy_state.rs` (746 ln), `widget/src/config.rs` (235), `widget/src/main.rs` (943), `widget/src/poller.rs` (475).
- **Baseline (must be reproduced before you start):**
  - `cargo build --frozen --quiet` → no output, exit 0.
  - `cargo test --frozen --quiet` → two lines: `test result: ok. 36 passed; 0 failed` (in-crate unit tests in `buddy_state.rs`/`config.rs`/`poller.rs`) and `test result: ok. 1 passed` (the `tests/screenshot_test.rs` golden test, ~48 s).
- **clippy and rustfmt are NOT installed** for this toolchain (`stable-x86_64-pc-windows-gnu`). Do **not** run `cargo clippy`/`cargo fmt`; rely on `cargo build --frozen` warnings and the golden test. (Task 0 makes this optional.)
- The golden-screenshot test is the **behavioral regression gate**: it renders all 4 phases (`SCENES = ["generating","prompting","idle","down"]`) to PNGs and compares against `widget/tests/snapshots/`. Any rename that changes a *value* or layout must keep it green; pure identifier renames are render-transparent.
- Naming conventions: repo `AGENTS.md` says "Naming of files and code should be intuitive and self-explanatory" and "Keep the documentation and wiki up to date." So every code rename that the wiki references must be mirrored in the wiki **in the same commit**.
- Docs that name widget internals (verified): `wiki/shared/architecture.md`, `wiki/shared/README.md`, `wiki/shared/parity/defects-2026-09-26.md`, `wiki/widget/modules/{buddy-state,config,poller,render}.md`, `wiki/widget/diagrams/{class-diagram,sequences}.md`. **No** doc references `lib.rs` (safe).

## Architecture / proposed approach
Four independent, behavior-preserving workstreams, each its own commit so any one can be reverted in isolation: (1) cosmetic + one latent-bug fix in `main.rs`; (2) a misleading field rename in `buddy_state.rs`; (3) the flagship `Conn` → `ConnState` enum rename (the `Conn::Ok`/`Result::Ok` collision is the main readability smell) mirrored into the wiki; (4) untracking build output that was accidentally committed. Every step is verified by the same two commands, so "done" is unambiguous.

## Conventions (apply to every task)
- One `git commit` per task. Commit message format: `refactor(widget): <what>` (or `fix(widget):`, `docs(widget):`, `chore(repo):`).
- After **each** code task run the full gate and confirm both lines pass before committing:
  ```
  cargo test --frozen --quiet 2>&1 | grep 'test result'
  ```
  Expected: `test result: ok. 36 passed; 0 failed` **and** `test result: ok. 1 passed`. (The screenshot test takes ~48 s — that is the gate; don't skip it for `main.rs` changes.)
- Use the `patch` tool (find/replace) for edits, not hand-rewrites. If a snippet has moved, `read_file` the region first and patch the current text.
- Work in `widget/` (the `cargo` commands are run from there). Docs/`.gitignore` edits are at repo root.

---

## Step-by-step tasks

### Task 0 — Capture the baseline (2 min)
Purpose: prove the starting point is green so a later failure is clearly *yours*.
1. From `widget/`:
   ```
   cargo build --frozen --quiet; echo "build=$?"
   cargo test --frozen --quiet 2>&1 | grep 'test result'
   ```
2. **Expected:** `build=0`; then `test result: ok. 36 passed; 0 failed` and `test result: ok. 1 passed`.
3. If the screenshot test does not pass here, **stop and tell the user** (the goldens are already red — don't proceed). Do **not** commit anything (no change).

(No commit — read-only.)

---

### Task 1 — `main.rs` cosmetic cleanups (behavior-preserving) (4 min)
All of these change **identifiers only** (and remove dead code) — values and layout are untouched, so the golden test must stay green.

In `widget/src/main.rs`:

1. **Rename the cryptic color const `PRE` → `PROMPTING`** (it is the prompting-state accent, matching `Phase::Prompting`).
   - Definition at line ~56:
     ```rust
     const PRE: egui::Color32 = egui::Color32::from_rgb(230, 180, 80); // #e6b450
     ```
     →
     ```rust
     const PROMPTING: egui::Color32 = egui::Color32::from_rgb(230, 180, 80); // #e6b450
     ```
   - Then replace **every** remaining `PRE` token with `PROMPTING`. It appears in `number_color`, `headline`, `capacity_line`, `draw_scene` (the badge dot), and `draw_data_card` (`if cap_warn { PRE }`). Use `replace_all` on the whole word `PRE` (it does not collide with any other identifier — verify with a `grep -n '\bPRE\b' src/main.rs` first; the only hits should be those usages).
2. **Rename `f_row_height` → `footnote_row_height`** (the `f_` prefix is meaningless; it is the height of the sparkline/footnote row). Function at line ~795 and its single call site at line ~747.
3. **Rename `leftover` → `remaining_line`** (it builds the "remaining work / phase status" line). Function at line ~208 and its call site at line ~701.
4. **Remove the dead statement** `let _ = left_rect;` at line ~709. (`left_rect` is used later at ~715 and ~747, so this line is a no-op.)
5. **Remove the no-op accessor `model_name`** (line ~250, body is just `st.model.clone()`) and inline it at its two call sites:
   - Line ~631: `&ws.servers.iter().map(|s| model_name(s)).collect::<Vec<_>>()` → `&ws.servers.iter().map(|s| s.model.clone()).collect::<Vec<_>>()`
   - Line ~676: `if let Some(name) = model_name(st) {` → `if let Some(name) = st.model.clone() {`
   - Delete the `fn model_name` block.

Verify:
```
cargo test --frozen --quiet 2>&1 | grep 'test result'
```
Expected: `ok. 36 passed` + `ok. 1 passed` (goldens unchanged — these are identifier-only changes).
Commit: `git commit -m "refactor(widget): clearer names in main.rs (PROMPTING, footnote_row_height, remaining_line), drop dead code"`

---

### Task 2 — `main.rs`: make the scene-name validation real + test (TDD) (5 min)
**The bug:** `scene_widget` (line ~820) matches the scene name with a catch-all `_ => {}` and **always returns `Some(ws)`**, so the "unknown scene" guard in `main()` (line ~910: `if scene_widget(&name).is_none() { ...exit(2) }`) is **unreachable** — a bad `DESKBUDDY_SCENE` is silently treated as idle instead of failing fast. The `.expect("scene handled in main")` in `BuddyApp::new` (line ~314) is likewise misleading.

**Step 2a — write the failing test.** `main.rs` is a bin, but `cargo test` still runs an in-file `#[cfg(test)]` module. Append to the end of `widget/src/main.rs`:
```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scene_widget_rejects_unknown_name() {
        assert!(scene_widget("nonsense").is_none());
    }

    #[test]
    fn scene_widget_builds_each_known_scene() {
        for name in SCENES {
            assert!(scene_widget(name).is_some(), "scene {name:?} should build");
        }
    }
}
```
Run and confirm the **new** test fails (red):
```
cargo test --frozen --quiet scene_widget_rejects_unknown_name 2>&1 | tail -5
```
Expected now: `test result: FAILED` (current code returns `Some` for any name). (The known-scene test already passes.)

**Step 2b — make `scene_widget` return `None` for unknown names.** Change the match inside `scene_widget` (line ~832) so an unrecognized name yields no state. Replace:
```rust
        match name {
            "generating" => { /* ... */ }
            "prompting"  => { /* ... */ }
            "idle"       => {}
            "down"       => s.feed_error(),
            _ => {}
        }
        let _ = label;
        s
    };
```
with a structure that returns `Option<BuddyState>` from the closure — the simplest correct edit: make `make` return `Option<BuddyState>` and have the catch-all produce `None`, then `map` both servers. Concretely, change the closure's tail so the catch-all arms diverge to `None`:
```rust
        let scene: Option<BuddyState> = match name {
            "generating" => { /* existing body, then */ Some(s) }
            "prompting"  => { /* existing body, then */ Some(s) }
            "idle"       => Some(s),
            "down"       => { s.feed_error(); Some(s) }
            _            => None,
        };
        scene
    };
```
> Note: keep the existing per-scene field assignments exactly as they are today; only wrap them so an unknown name yields `None`. If the closure shape makes that awkward, extract a `fn seeded_server(host: &str, name: &str) -> Option<BuddyState>` that contains the current `make` body and returns `None` in the catch-all, and have `scene_widget` call it twice, aborting to `None` if either is `None`.

Also **remove the now-dead `label` parameter** from the `make` closure (it was only used by `let _ = label;`): `make(host, label)` → `make(host)`; update the two call sites `make("m.tower1:11444", "tower1")` → `make("m.tower1:11444")` and `make("m.mini1:11444", "mini1")` → `make("m.mini1:11444")`. Do the same for `live_widget`'s `mk` closure (line ~867): `mk(label)` → `mk()` (drop the param and the `let _ = label;`), updating `mk("tower1")`/`mk("mini1")` → `mk()`/`mk()`.

Now `scene_widget` returns `None` for a bad name, so `main()`'s `exit(2)` guard and the `.expect(...)` are both live and correct.

**Step 2c — verify green.**
```
cargo test --frozen --quiet 2>&1 | grep 'test result'
```
Expected: `test result: ok. 38 passed; 0 failed` (36 + the 2 new unit tests) and `test result: ok. 1 passed`. The screenshot test still passes because it only ever calls `scene_widget` with the four valid `SCENES` names.
Commit: `git commit -m "fix(widget): scene_widget returns None for unknown names so the bad-scene guard works"`

---

### Task 3 — `buddy_state.rs`: rename misleading `decode_total` → `decoded` (4 min)
`decode_total` holds the processing slot's `n_decoded` **for this poll** (it is overwritten each poll, not accumulated). The word "total" misleads readers into thinking it's a running sum. Rename to `decoded`.

In `widget/src/buddy_state.rs`:
- Field (line ~132): `pub decode_total: u64,` → `pub decoded: u64,`
- `Default` (line ~194): `decode_total: 0,` → `decoded: 0,`
- `feed_ok` (line ~241): `self.decode_total = decode_total;` → `self.decoded = decode_total;` (the *parameter* `decode_total` stays — it's the incoming value; only the field changes. If you prefer, also rename the param to `decoded`, but then update the call sites in `poller.rs` and `main.rs` — **keep the param as-is** to scope this to `buddy_state.rs`.)
- `phase()` (line ~401): `if self.decode_total == 0 {` → `if self.decoded == 0 {`
- Tests that read the field: search `decode_total` inside the `tests` module; only the **field reads** (e.g. `s.decode_total`) become `s.decoded`. Leave `feed_ok(...)` argument positions untouched (they're positional).
- **Cross-file field reads:** `main.rs` sets the field directly in `scene_widget` (line ~835 `s.decode_total = 4_200;` and ~842 `s.decode_total = 0;`) → change both to `s.decoded`. `poller.rs` only passes it as a positional arg to `feed_ok` (no field read) — no change.

Verify there are no stragglers:
```
grep -rn '\bdecode_total\b' src tests
```
Expected: only the `feed_ok` **parameter** name remains (that is intentional). Any remaining `self.decode_total` / `s.decode_total` is a miss.

Run the gate (`cargo test --frozen --quiet 2>&1 | grep 'test result'`): expected `ok. 38 passed` + `ok. 1 passed`.
Commit: `git commit -m "refactor(widget): rename BuddyState.decode_total to decoded (it is a per-poll value)"`

---

### Task 4 — `buddy_state.rs`: rename `Conn { Ok, ServerDown }` → `ConnState { Up, Down }` (5 min)
`Conn::Ok` reads like `Result::Ok` and hides the meaning; `ServerDown` is long. `ConnState { Up, Down }` is the intuitive pair. (It is a **different** enum from `Phase`; `Phase::Down` is a rendering concern, `ConnState::Down` is a connectivity concern — the type distinction keeps them apart.)

In `widget/src/buddy_state.rs`, apply these replacements (use `replace_all` where safe):
- `enum Conn {` → `enum ConnState {` (and the closing of its variants below)
- Variant `Ok,` → `Up,` and `ServerDown,` → `Down,` **within the enum definition only** (lines ~71–73).
- `pub conn: Conn,` (line ~120) → `pub conn: ConnState,`
- `conn: Conn::ServerDown,` (Default, line ~189) → `conn: ConnState::Down,`
- `self.conn = Conn::Ok;` (feed_ok, ~238) → `self.conn = ConnState::Up;`
- `self.conn = Conn::ServerDown;` (feed_error, ~308) → `self.conn = ConnState::Down;`
- `if self.conn != Conn::Ok {` (phase, ~394) → `if self.conn != ConnState::Up {`
- `poll_interval` match arms (~432–437): `(Conn::Ok, true)` → `(ConnState::Up, true)`, `(Conn::Ok, false)` → `(ConnState::Up, false)`, `(Conn::ServerDown, _)` → `(ConnState::Down, _)`
- The doc comment on the enum (lines ~66–67) mentions the states; update wording to "Up / Down" if it names the old variants.
- In-file `tests` module: `Conn::Ok` → `ConnState::Up`, `Conn::ServerDown` → `ConnState::Down` (several assertions).

**Cross-file:** `widget/src/main.rs` line ~831: `s.conn = buddy_state::Conn::Ok;` → `s.conn = buddy_state::ConnState::Up;`. `poller.rs` does **not** name `Conn` directly (it calls `feed_ok`/`feed_error`) — no change. `config.rs` — no change.

Verify no stragglers:
```
grep -rn '\bConn\b\|Conn::' src
```
Expected: zero matches for the bare `Conn` type / `Conn::` (all now `ConnState`). Run the gate: `ok. 38 passed` + `ok. 1 passed`.
Commit: `git commit -m "refactor(widget): rename Conn{Ok,ServerDown} to ConnState{Up,Down}"`

---

### Task 5 — Align the wiki docs with the renames (4 min)
`AGENTS.md` requires docs to track the code. The renames above change symbols the wiki names. From the repo root:
1. `ConnState` (from Task 4) is referenced in:
   - `wiki/widget/diagrams/class-diagram.md` — `class Conn {`, `+Conn conn`, `BuddyState --> Conn : has`, and the prose "`Conn` is two-variant on the desktop: `Ok` / `ServerDown`".
   - `wiki/widget/modules/buddy-state.md` — `Conn::Ok` / `Conn::ServerDown`, `pub enum Conn { Ok, ServerDown }`.
   - `wiki/shared/architecture.md` — "`BuddyState` + `Conn` per server".
   Update each: `Conn` → `ConnState`, `Ok` → `Up`, `ServerDown` → `Down`, preserving the surrounding explanation. Keep the note that "Idle is `conn == Up && working == false`, not a variant" (adjust the variant name).
2. `decode_total` (Task 3): grep `grep -rn 'decode_total' wiki README.md` and update any hit to `decoded`.
3. `PRE`, `f_row_height`, `leftover`, `model_name` (Task 1): grep these across `wiki/` and `README.md`; they are `main.rs`-internal and almost certainly not documented, but confirm. (Expected: no hits — if a hit exists, update it.)

Verify:
```
grep -rn 'Conn::Ok\|Conn::ServerDown\|enum Conn \|+Conn conn\|decode_total' wiki README.md
```
Expected: **no output** (all old spellings gone). Do **not** change `main.rs` file references in the docs — `main.rs` is still the correct render-module path.
Commit: `git commit -m "docs(widget): track ConnState/decoded renames in wiki + README"`

---

### Task 6 — Untrack accidentally-committed build output (chore) (5 min)
The repo currently tracks generated files that should be gitignored (build binaries, `.bak`, `__pycache__`, throwaway scratch PNGs). Untrack them from git (keep them on disk — do **not** delete working files) and add ignores so they stay out.

1. Confirm what's tracked (sanity check):
   ```
   git ls-files | grep -Ei 'build/|\.bak$|__pycache__|screenshot/|test_sketch/.*\.png|cap_weather|cache_weather_sim'
   ```
2. Add to the root `.gitignore` (append; create if absent — one already exists):
   ```
   # build output & generated
   **/build/
   *.bak
   __pycache__/
   *.pyc
   # throwaway captures
   screenshot/
   esp32s3_272x792/fw/test_sketch/*.png
   esp32s3_272x792/fw/weather_test/cap_weather.*
   esp32s3_272x792/tools/cache_weather_sim.png
   ```
3. Untrack the existing paths (keeps the files on disk, removes from index):
   ```
   git rm -r --cached \
     esp32c6_480x480/firmware/build \
     esp32c6_480x480/firmware/firmware_static.ino.bak \
     esp32c6_480x480/firmware/tools/__pycache__ \
     esp32s3_272x792/fw/weather_test/build \
     esp32s3_272x792/fw/weather_test/cap_weather.bin \
     esp32s3_272x792/fw/weather_test/cap_weather.png \
     esp32s3_272x792/fw/test_sketch \
     esp32s3_272x792/tools/cache_weather_sim.png \
     screenshot \
     tools/__pycache__ \
     shared/scene-assets/__pycache__
   ```
   > Adjust the list to exactly what `git ls-files` shows for the patterns above — do not `rm` a path that isn't tracked. `test_sketch` and `screenshot` may contain a few files that are *intended* keepers; if any are, untrack only the `*.png`/scratch ones and keep those tracked.
4. Verify the working tree is untouched and the index is clean of these:
   ```
   git status --short | grep -Ei 'build/|\.bak|__pycache__|\.png' 
   git ls-files | grep -Ei 'build/|\.bak$|__pycache__' | wc -l
   ```
   Expected: the second command prints `0`; the first shows only the `D ` (deleted-from-index) entries for the untracked paths, **no** modified/added working files.
5. Re-run the build gate to be sure nothing that was on disk got deleted: `cargo test --frozen --quiet 2>&1 | grep 'test result'` → `ok. 38 passed` + `ok. 1 passed`.
Commit: `git commit -m "chore(repo): untrack build output, .bak, __pycache__, and scratch PNGs"`

---

## Tests / validation
- **Gate (after every code task):** `cargo test --frozen --quiet 2>&1 | grep 'test result'` → `ok. 38 passed; 0 failed` (Tasks 1–4 add 2 unit tests; before Task 2 it's 36) **and** `ok. 1 passed` (the golden screenshot test — the behavioral proof that rendering is unchanged).
- **The TDD cycle** is isolated to Task 2: red (`scene_widget_rejects_unknown_name` fails) → fix → green (38 passed). All other tasks are behavior-preserving refactors where the existing 36 unit tests + the golden test are the red/green signal (an incomplete rename makes `cargo build --frozen` fail = red; fix = green).
- **Grep gates** after each rename (Tasks 3–5) confirm no stragglers; each has an exact expected output above.
- Final whole-task proof: `git log --oneline` shows one commit per task; `git status` is clean.

## Risks, tradeoffs, and open questions
- **Golden test is the safety net, and it is the slow part (~48 s).** Every task re-runs it. That is intentional — it's the only proof that no rename leaked into rendering. If a task turns the goldens red, the change altered a value/layout (not just a name): revert that task's code and re-examine; do not edit the goldens to "make it pass."
- **The `Conn` → `ConnState` rename is the highest-churn item** (~12 call sites across `buddy_state.rs` + 1 in `main.rs` + 3–4 wiki files). It is the single most readability-worthwhile fix, but if you want to de-risk, Tasks 1–3 and 5–6 are independent and can be done first; Task 4 + its doc update (folded into Task 5) can be a separate PR. **Open question:** do you want `ConnState { Up, Down }` or to keep the long-but-explicit `ServerDown` variant? The plan uses `Up`/`Down`.
- **`decode_total` param vs field (Task 3):** I deliberately keep the `feed_ok` *parameter* named `decode_total` to scope the change to `buddy_state.rs` + the two `main.rs` field writes. If you'd rather rename the param too, that also touches `poller.rs`'s `feed_ok` call sites (positional, so only the local bindings) — say the word and I'll extend the plan.
- **Task 6 touches git history/working-tree perception.** It only `git rm --cached` (keeps files on disk) and edits `.gitignore`. No file is deleted. **Open question:** are the `screenshot/` PNGs and `test_sketch/*.png` throwaway, or do you keep some as design references? If some are keepers, list them and I'll narrow the untrack list.
- **Out of scope (left as-is):** the `toks_per_s` vs `tps`/`session_avg_tps` naming mix (consistent enough), the duplicated Down/footnote check in `row_content_h`/`footnote_row_height` (two one-line constants, not worth a helper), and any `poller.rs`/`config.rs` rename (both are already clean and well-documented). clippy/rustfmt are not installed; enabling them needs `rustup component add clippy rustfmt` (network) — deliberately not assumed.
- **Docs that reference `main.rs` as the render module are already correct** — no doc work is needed for file names, only for the renamed symbols (Tasks 4–5).
