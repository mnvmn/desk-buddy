# Module: `buddy_state`

The state machine + tok/s math. **Shared logic** — the one piece both the
device and widget reuse. **Implemented in the widget** as
[`desktop-llama/src/buddy_state.rs`](../../../desktop-llama/src/buddy_state.rs): the
per-server `BuddyState` + `ConnState` + `Phase` (the device mirrors in C), plus
the multi-server wrapper `WidgetState`, the `PollTuning` config type, and the
`SharedState` alias. Pure logic with I/O injected, so it's unit-testable with
no hardware, display, or network.

## Responsibilities

- Hold the per-server struct that drives one row of the UI (`BuddyState`).
- Wrap the whole card in `WidgetState` — `Vec<BuddyState>` (one per
  configured server) + their labels + a single shared `DisplayMode` — so one
  mode toggle flips the whole card and the poller can update all servers
  atomically.
- Classify connection state: `ConnState::Up` (endpoint answered) /
  `ConnState::Down` (endpoint unreachable / parse failure). The desktop has
  no Wi-Fi to lose, so there is **no `OFFLINE_WIFI`**; the device adds it.
- Classify the **phase** the row should render (`phase()`): `Down`, `Idle`,
  **`Prompting`** (in-flight, prompt still being processed — `working` but
  `decoded == 0`), `Generating` (tokens decoding). `Prompting` is the
  state the old two-state (working / not) model could not show. A Gufo server
  has no live work signal — it is `Generating` only while `gufo_active` (a
  request completed within the last few `/metrics` polls), else `Idle`; there
  is no `Prompting` for Gufo.
- Compute `working` = `any(slot.is_processing)`.
- Compute tok/s (derived, **the only live path** for a llama.cpp server):
  Δ(sum of `next_token[].n_decoded`) / **real elapsed** between polls. First
  working cycle shows `—` (no usable baseline). The `/metrics` gauge is **not**
  used as a source for llama.cpp — it lags (see
  [architecture.md](../../shared/architecture.md)). **Gufo is the exception:**
  it exposes no live decode counter, so its headline **is** the `/metrics`
  `predicted_tokens_seconds` gauge (last completed request's speed), and its
  *activity* is decided by freshness (`gufo_active`), not by the gauge being
  present (the gauge holds its value while idle).
- Derive an **ETA**: `eta_secs() = n_remain / toks_per_s` when both are
  known (`n_remain > 0` and a live rate); `None` otherwise (unbounded or no
  rate yet).
- Decay the displayed value to 0 when `working` goes false — never freeze the
  last number.
- Hold **best-effort context** from the slow paths: model / quant / ctx
  (`feed_props`) and session avg + KV-cache reuse (`feed_metrics`). These
  never affect `conn` or the rate — the hot path works with all of them
  `None`.
- Provide the config-driven poll cadence (`PollTuning` + `poll_interval(&tuning)`).

## Structs (implemented, `buddy_state.rs`)

```rust
pub enum ConnState { Up, Down }                 // desktop (device adds OFFLINE_WIFI)
pub enum Protocol { Llama, Gufo }               // Llama (default) — from the /slots shape
pub enum Phase { Down, Idle, Prompting, Generating }   // implements Display
pub enum DisplayMode { Data, Scene }          // shared across the whole card

/// Per-server poll cadence, from the config file (clamped 200–10 000).
pub struct PollTuning { pub working_ms: u64, pub idle_ms: u64, pub error_ms: u64 }
// Default: 500 / 2000 / 5000

/// One inference server — drives one row of the card / one scene band.
pub struct BuddyState {
    pub conn: ConnState,
    pub working: bool,                          // any is_processing
    pub toks_per_s: f64,                        // 0.0 when idle/prompting or first cycle
    pub n_remain: i64,                          // busy slot's n_remain (-1 = unbounded)
    pub decoded: u64,                           // processing slot's n_decoded; 0 while prefill → drives Prompting
    pub protocol: Protocol,                     // Llama (default) | Gufo — from the /slots shape (see poller)
    pub gufo_active: bool,                      // Gufo: a request completed within the last GUFO_ACTIVE_POLLS polls (~8 s)
    gufo_pred_total: Option<u64>,               // Gufo: baseline tokens_predicted_total (advances only on completion)
    gufo_idle_calls: u32,                       // Gufo: flat /metrics feeds since last completion (decays gufo_active)
    pub host_label: Option<String>,             // config label (e.g. "tower1")
    pub host: Option<String>,                   // endpoint (set by the poller)
    pub model: Option<String>,                  // /props: model_alias (else model_path base)
    pub quant: Option<String>,                  // /props: model_ftype
    pub ctx_tokens: Option<u64>,                // window: /props n_ctx (slot n_ctx backs up)
    pub ctx_used: Option<u64>,                  // /slots: n_prompt_tokens (live occupancy)
    pub kv_reuse: Option<f64>,                  // /metrics: cached/total prompt tokens, 0..1
    pub session_avg_tps: Option<f64>,           // /metrics gauge — LAGGING; footnote only (llama.cpp) but the headline (Gufo)
    rate_hist: Vec<f64>,                        // last ≤32 live tok/s samples → sparkline
    last_decode_total: Option<u64>,             // running counter baseline; None after an error
}

/// The whole card — one `BuddyState` per configured server.
#[derive(Debug, Clone)]
pub struct WidgetState {
    pub servers: Vec<BuddyState>,
    pub labels: Vec<String>,                    // config labels, parallel to `servers`
    pub display_mode: DisplayMode,
}

pub type SharedState = Arc<Mutex<WidgetState>>; // poller writes, UI clones per frame
```

Key methods: `feed_ok(is_processing, decode_total, n_remain, n_prompt_tokens, n_ctx, prompt_processed, prompt_total, elapsed)` (llama.cpp hot path),
`feed_gufo(model, n_slots)` (Gufo hot path — no live decode state), `feed_error()`,
`feed_props(model, quant, ctx_tokens)`,
`feed_metrics(avg_tps, cached, total, spec_accepted, spec_draft, predicted_total)`,
`gufo_speed() -> Option<f64>` (the Gufo headline gauge), `phase()`, `eta_secs()`,
`ctx_usage() -> Option<f64>` (live context fill, 0..1),
`rate_history() -> &[f64]`, `set_rate_hist(Vec<f64>)` (test/scene seeding),
`poll_interval(&tuning: &PollTuning) -> Duration`. On `WidgetState`:
`display_mode` toggle helpers (`toggle_mode`, `set_mode`).

## Rules

- **Rate only on a valid delta:** tok/s is computed only when
  `working`, a previous baseline exists, **and** the counter advanced
  (`decode_total > prev > 0`). Baseline `0` = the idle→working transition
  (first cycle → `—`) or a fresh request's counter reset — both mean "no rate
  this cycle." This is what kills the false spike at the start of generation.
- **Prompting vs Generating:** both have `working == true`; they differ only
  by `decoded`. `0` → `Prompting` (prompt being processed), `> 0` →
  `Generating`. The transition is one poll later when the first token decodes.
- **Real elapsed**, injected by the caller (`Instant::elapsed()`), never the
  nominal interval.
- **Cadence** from the resulting state: 500 ms working (incl. `Prompting`),
  2 s idle, 5 s error (see [poller](poller.md)).
- **`feed_error()`** sets `Down`, zeroes `working`/tok/s/`n_remain`/
  `decoded`, and clears `last_decode_total` so recovery snaps back to
  `Up` with a fresh baseline (no stale Δ). It also clears the **session
  context** — `session_avg_tps`, `kv_reuse`, `spec_acceptance`, `tokens_total`
  and `rate_hist` — and resets the Gufo activity state (`gufo_active`,
  `gufo_idle_calls`, `gufo_pred_total`) so the down state shows no stale stats,
  sparkline, or "just finished" green. It does **not** clear the model /
  quant / ctx (`/props` data) — the endpoint going down doesn't un-know the
  model name.
- **Sparkline history:** `feed_ok` pushes every *valid* tok/s sample into
  `rate_hist` (capped at 32, oldest dropped first) — the sole source of the
  render-layer sparkline. Idle keeps the history (the bars show what just
  happened); only `feed_error` clears it.
- **Live context usage:** `feed_ok` stores the first slot's
  `n_prompt_tokens` in `ctx_used` (live context occupancy, refreshed every
  hot poll) and uses the slot's `n_ctx` to **back up** `ctx_tokens` when
  `/props` never delivered a window. `ctx_usage()` = `ctx_used / ctx_tokens`
  clamped to 0..1, `None` until both are known. `feed_error()` clears
  `ctx_used` (no live figure while down) but keeps `ctx_tokens` (same rule
  as the model name — going down doesn't un-know the window).
- **`feed_props`** applies only non-empty values, so a partial `/props`
  response can't blank a known field.
- **`feed_metrics`** stores the gauge and computes `kv_reuse = cached/total`
  clamped to 0..1 (`None` when `total == 0`), `spec_acceptance =
  accepted/draft` (`None` when no drafts), and `tokens_total =
  prompt_tokens_total + tokens_predicted_total`. The two cumulative counters
  are the robust reuse/total signal; the gauge is lagging and footnote-only
  **for llama.cpp**. It also drives **Gufo activity**: Gufo exposes no live
  work flag, so the cumulative `tokens_predicted_total` counter advancing is
  the only "a request just finished" evidence — an increase sets
  `gufo_active` true and resets the idle counter; a flat feed increments the
  idle counter, and once it exceeds `GUFO_ACTIVE_POLLS` (= 4, ~8 s at the
  2 s cadence) `gufo_active` goes false. No-op for llama servers (they have a
  live decode counter).
- Bad input from the poller is fed in as an error — this module never
  panics on it.

## Unit tests (`cargo test`)

Per-server phase machine: `idle_is_healthy_and_zero`,
`first_working_cycle_has_no_rate`, `prefill_is_prompting_phase`,
`prefill_resolves_to_generating_when_decode_starts`,
`delta_rate_over_real_elapsed`, `counter_reset_clears_rate`,
`eta_from_remain_and_rate`, `eta_none_without_bound_or_rate`,
`error_clears_everything`, `recovery_snaps_back_to_ok`,
`metrics_kv_reuse_ratio`, `metrics_ratio_clamped`, `metrics_spec_acceptance_ratio`,
`metrics_tokens_total_is_prompt_plus_generated`, `props_applies_only_non_empty`,
`sparkline_accumulates_decode_rates`,
`down_clears_session_stats_and_sparkline`, `ctx_usage_ratio_and_clamp`,
`ctx_usage_needs_window_size`, `down_clears_live_ctx_occupancy`,
`slots_count_stored_and_ignores_zero`.
Gufo: `gufo_protocol_and_speed_from_gauge`,
`gufo_speed_none_for_llama_and_zero_gauge`, `gufo_does_not_show_stale_live_rate`.
Multi-server: `display_mode`
toggle, `poll_interval_uses_config_tuning`.

## Dependencies

- **Used by:** [render](render.md) (widget), [ui](../../amoled-panel-llama/modules/ui.md) (device),
  [poller](poller.md) (feeds it)
- **Uses:** nothing external — pure logic (`std::time::Duration`).

## Gotchas

- `next_token` is an **array** — the poller takes the **processing** slot's
  `n_decoded` (parity P2; NOT a sum over all slots) and this module just
  receives that value.
- Idle, prompting, and down must stay distinct end to end; the render layer is
  where they're easiest to conflate.
- Don't reintroduce the gauge as the live source **for llama.cpp** — it lags
  behind generation. Its only job there is the session-average footnote.
  **Gufo is the deliberate exception:** it has no live decode counter, so the
  gauge is its headline number — but its *activity* comes from freshness
  (`gufo_active`, i.e. the `tokens_predicted_total` counter advancing), never
  from the gauge being present (the gauge holds its last value while idle,
  which is why an idle box used to look "generating").

See [shared/API_ENDPOINTS.md](../../../shared/API_ENDPOINTS.md) for exact
field names.
