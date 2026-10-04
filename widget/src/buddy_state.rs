//! Pure state machine + tok/s math for DeskBuddy.
//!
//! No I/O here — `poller.rs` feeds this struct and the UI reads it.
//! Keeping it pure makes it unit-testable without hardware or a server
//! (spec §9 Testability).
//!
//! Data has three sources (see `shared/API_ENDPOINTS.md`):
//!   * `/slots`   — hot path, every poll  → working, decode, n_remain → live rate
//!   * `/props`   — once at startup        → model name, quant, ctx
//!   * `/metrics` — slow, background (~2s) → session-average tok/s + KV-cache reuse
//!
//! The hot path never depends on the slow ones: if `/metrics` or `/props`
//! are missing or 404, the card still works — those fields just stay `None`.

use std::fmt;
use std::sync::{Arc, Mutex};
use std::time::Duration;

/// The shared widget state: one `BuddyState` per inference server (stacked
/// vertically on the card, top = first) plus the server labels and the shared
/// display mode. Written by the poller thread, read by the UI each frame.
#[derive(Debug, Clone)]
pub struct WidgetState {
    /// One per configured server, in card order (index 0 = top).
    pub servers: Vec<BuddyState>,
    /// Short names for the per-server footer (index 0 = top).
    pub labels: Vec<String>,
    /// Shared across the whole card (one chrome button pair toggles it).
    pub display_mode: DisplayMode,
}

impl WidgetState {
    /// Switch the whole card between the data layout and the combined scene.
    pub fn toggle_mode(&mut self) {
        self.display_mode = match self.display_mode {
            DisplayMode::Data => DisplayMode::Scene,
            DisplayMode::Scene => DisplayMode::Data,
        };
    }

    /// Set the display mode explicitly (config restore / tests).
    pub fn set_mode(&mut self, mode: DisplayMode) {
        self.display_mode = mode;
    }
}

/// Shared between the poller thread (writer) and the UI (reader).
pub type SharedState = Arc<Mutex<WidgetState>>;

/// Poll cadence (ms) — extracted here so it's tunable from the config file
/// (`config::Config::poll`). `None`/`Default` are the built-ins (spec §3.3):
/// fast while working, slower when idle, throttled when the endpoint is down.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PollTuning {
    pub working_ms: u64,
    pub idle_ms: u64,
    pub error_ms: u64,
}

impl Default for PollTuning {
    fn default() -> Self {
        Self { working_ms: 500, idle_ms: 2000, error_ms: 5000 }
    }
}

/// Connectivity. A desktop widget has no Wi-Fi to lose, so there is no
/// `OFFLINE_WIFI` state — only endpoint reachability.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnState {
    /// A `/slots` poll succeeded — the server is up.
    Up,
    /// The last `/slots` poll failed — the server is down.
    Down,
}

/// What the server is doing right now, as the card should render it.
/// Derived from `(conn, working, whether tokens have started decoding)`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Phase {
    /// Endpoint unreachable — error accent.
    Down,
    /// Endpoint up, nothing generating — healthy, dimmed.
    Idle,
    /// A request is in flight but the prompt is still being processed
    /// (`is_processing` true, no decode tokens yet). The state the old
    /// two-state model (working / not) could not show.
    Prompting,
    /// Tokens are being generated — live tok/s.
    Generating,
}

/// Human-readable phase names (the poller's per-poll log line).
impl fmt::Display for Phase {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Phase::Generating => "generating",
            Phase::Prompting => "prompting",
            Phase::Idle => "idle",
            Phase::Down => "down",
        };
        f.write_str(s)
    }
}

/// What the card shows. The data card is the default; **scene** swaps it for
/// the full-card pixel art ("Plumber's run") — the level's frame for the
/// current phase (sprinting when busy, calm level when idle). Toggled by the
/// card's bottom-right button / right-click menu; persists with the other
/// chrome settings once M3 lands config persistence.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DisplayMode {
    #[default]
    Data,
    Scene,
}

/// The single struct that drives the whole UI (spec §4, desktop-adapted).
#[derive(Debug, Clone)]
pub struct BuddyState {
    pub conn: ConnState,
    /// Any slot `is_processing`.
    pub working: bool,
    /// Derived generation tok/s; 0.0 when idle/prompting or on the first
    /// working cycle.
    pub toks_per_s: f64,
    /// `next_token[].n_remain` of the busy slot (`-1` = unbounded).
    pub n_remain: i64,
    /// Tokens decoded in the current request, this poll. The rate source
    /// (parity P2): the **processing** slot's `n_decoded` only (see
    /// `poller::fetch_slots`) — not a sum over all slots. `0` while the prompt
    /// is still being processed (prefill) → the `Prompting` phase.
    pub decoded: u64,

    // ---- model / session context (best-effort, from /props + /metrics) ----
    /// Model name from `/props` (`model_alias`, else `model_path` basename).
    pub model: Option<String>,
    /// Quantization type from `/props` (`model_ftype`), e.g. `"Q4_K - Medium"`.
    pub quant: Option<String>,
    /// Context window from `/props` (`default_generation_settings.n_ctx`).
    pub ctx_tokens: Option<u64>,
    /// Tokens **currently held in the context** from the live `/slots` poll —
    /// `sum(n_prompt_tokens + n_tokens)` across ALL slots, so generated
    /// tokens count as they accumulate and the last request's occupancy
    /// shows while idle (the device firmware counts exactly this, so both
    /// targets read the same number — parity P1/P5). `None` until the first
    /// poll (or if the server version doesn't report it).
    pub ctx_used: Option<u64>,
    /// The host string this server polls (for the dim `label · host` footer),
    /// set from the config by the poller.
    pub host_label: Option<String>,
    /// Prompt KV-cache reuse ratio 0.0–1.0 (`/metrics` cumulative counters).
    pub kv_reuse: Option<f64>,
    /// Session-average generation tok/s from the `/metrics` gauge. LAGGING —
    /// footnote only, never the headline number (probe: reads 0 during a run).
    pub session_avg_tps: Option<f64>,
    /// Speculative-decode acceptance ratio 0.0–1.0 (`/metrics`
    /// `speculative_tokens_accepted_total / ..._draft_total`). Cumulative, so
    /// it's a stable per-server rate — shown as a `spec N%` footnote.
    pub spec_acceptance: Option<f64>,
    /// Total tokens processed this server run (prompt + generated), from the
    /// `/metrics` cumulative counters `prompt_tokens_total +
    /// tokens_predicted_total`. Resets to 0 on server restart. Shown as a
    /// `NN.Nk total` footnote chip. `None` until the first `/metrics` pass.
    pub tokens_total: Option<u64>,
    /// Number of parallel inference slots the server exposes
    /// (`/slots` array length) — e.g. `2` for two concurrent requests.
    /// `None` until the first successful poll (or a server that reports 0).
    /// Shown as a leading `×N slots` chip on the capacity line.
    pub n_slots: Option<u64>,
    /// Prefill progress 0.0–1.0 for the active request:
    /// `n_prompt_tokens_processed / n_prompt_tokens` of the processing slot.
    /// `None` when idle / not reported. Drives the Prompting headline (`42%`).
    pub prefill_frac: Option<f64>,
    /// Live prefill rate (tokens/s) while the prompt is being processed:
    /// Δ`n_prompt_tokens_processed` / Δ real elapsed. 0.0 until the second
    /// working cycle of a prefill.
    pub prefill_tps: f64,

    /// Running counter baseline for the Δ. `None` after an error.
    last_decode_total: Option<u64>,
    /// Running baseline of `n_prompt_tokens_processed` for the prefill Δ.
    last_prefill_total: Option<u64>,
    /// Recent live tok/s samples (oldest → newest), capped — drives the sparkline.
    rate_hist: Vec<f64>,
}

impl Default for BuddyState {
    fn default() -> Self {
        Self {
            conn: ConnState::Down, // unknown until the first poll succeeds
            working: false,
            toks_per_s: 0.0,
            n_remain: -1,
            decoded: 0,
            model: None,
            quant: None,
            ctx_tokens: None,
            ctx_used: None,
            host_label: None,
            kv_reuse: None,
            session_avg_tps: None,
            spec_acceptance: None,
            tokens_total: None,
            n_slots: None,
            prefill_frac: None,
            prefill_tps: 0.0,
            last_decode_total: None,
            last_prefill_total: None,
            rate_hist: Vec::new(),
        }
    }
}

impl BuddyState {
    /// Record one successful `/slots` poll (hot path).
    ///
    /// `decoded` = the **processing** slot's `next_token[].n_decoded`
    /// (parity P2 — see `poller::fetch_slots`; not a sum over all slots, or an
    /// idle slot's stale counter breaks the rate).
    /// `elapsed` = **real** wall time since the previous successful poll
    /// (not the nominal interval — slow polls must not skew the number).
    /// `n_prompt_tokens` / `n_ctx` are the first slot's live context
    /// occupancy (how full the context window is right now).
    /// `prompt_processed` / `prompt_total` = the busy slot's
    /// `n_prompt_tokens_processed` / `n_prompt_tokens` (prefill progress +
    /// rate); both `None` when nothing is processing.
    pub fn feed_ok(
        &mut self,
        is_processing: bool,
        decode_total: u64,
        n_remain: i64,
        n_prompt_tokens: Option<u64>,
        n_ctx: Option<u64>,
        prompt_processed: Option<u64>,
        prompt_total: Option<u64>,
        elapsed: Duration,
    ) {
        self.conn = ConnState::Up;
        self.working = is_processing;
        self.n_remain = n_remain;
        self.decoded = decode_total;
        self.ctx_used = n_prompt_tokens;
        // The slot's own n_ctx backs up /props (same number, fresher source).
        if self.ctx_tokens.is_none() {
            self.ctx_tokens = n_ctx;
        }

        // Prefill progress + live rate, from the busy slot's processed/total.
        let base = self.last_prefill_total;
        self.last_prefill_total = prompt_processed;
        match (prompt_processed, prompt_total) {
            (Some(p), Some(t)) if t > 0 => {
                self.prefill_frac = Some((p as f64 / t as f64).clamp(0.0, 1.0));
                if is_processing {
                    if let Some(prev) = base {
                        let dt = elapsed.as_secs_f64();
                        self.prefill_tps = if p > prev && dt > 0.0 {
                            (p - prev) as f64 / dt
                        } else {
                            0.0
                        };
                    } else {
                        self.prefill_tps = 0.0;
                    }
                } else {
                    self.prefill_tps = 0.0;
                }
            }
            _ => {
                self.prefill_frac = None;
                self.prefill_tps = 0.0;
            }
        }

        let baseline = self.last_decode_total;
        self.last_decode_total = Some(decode_total);

        if is_processing {
            match baseline {
                // Only trust a delta when the counter actually advanced from a
                // non-zero baseline. Baseline 0 = transition from idle (first
                // working cycle → no rate yet), or a counter reset from a new
                // request — both mean: no rate this cycle.
                Some(prev) if prev > 0 && decode_total > prev => {
                    let dt = elapsed.as_secs_f64();
                    if dt > 0.0 {
                        self.toks_per_s = (decode_total - prev) as f64 / dt;
                        // Record the sample for the sparkline (positive rates
                        // only — idle/prefill cycles would flatten the bars).
                        self.rate_hist.push(self.toks_per_s);
                        if self.rate_hist.len() > 32 {
                            self.rate_hist.remove(0);
                        }
                    }
                }
                _ => self.toks_per_s = 0.0,
            }
        } else {
            // Idle is a healthy state, not an error. Do not freeze the last
            // number — decay to 0 (spec §4). The history keeps the last
            // activity, so the sparkline still shows what just happened.
            self.toks_per_s = 0.0;
        }
    }

    /// Record a failed poll (timeout, refused, 4xx/5xx, parse error → B9).
    pub fn feed_error(&mut self) {
        self.conn = ConnState::Down;
        self.working = false;
        self.toks_per_s = 0.0;
        self.n_remain = -1;
        self.decoded = 0;
        self.ctx_used = None; // no live context figure while down
        self.last_decode_total = None; // snap back to OK with a fresh baseline
        self.last_prefill_total = None;
        self.prefill_frac = None;
        self.prefill_tps = 0.0;
        self.rate_hist.clear(); // a restart starts the sparkline fresh
        self.session_avg_tps = None; // no stale session stats in the down state
        self.kv_reuse = None;
        self.spec_acceptance = None;
        self.tokens_total = None;
    }

    /// Recent live tok/s samples (oldest → newest), for the sparkline.
    pub fn rate_history(&self) -> &[f64] {
        &self.rate_hist
    }

    /// Replace the rate history (scene/test seeding only — the poller builds
    /// it via `feed_ok`).
    pub fn set_rate_hist(&mut self, hist: Vec<f64>) {
        self.rate_hist = hist;
    }

    /// Record a `/metrics` sample (slow path). Best-effort: a failed fetch
    /// simply never calls this, leaving the fields at their previous value.
    ///
    /// `avg_tps` is the lagging `predicted_tokens_seconds` gauge (footnote
    /// only). `cached`/`total` are the cumulative prompt-token counters; the
    /// reuse ratio is their quotient clamped to 0..1. `spec_accepted` /
    /// `spec_draft` are the cumulative speculative-decode counters; their
    /// quotient is the stable acceptance ratio (a `spec N%` footnote).
    /// `predicted_total` is the cumulative generation-token counter;
    /// `total + predicted_total` is the session total tokens processed
    /// (a `NN.Nk total` footnote chip).
    pub fn feed_metrics(&mut self, avg_tps: f64, cached: u64, total: u64, spec_accepted: u64, spec_draft: u64, predicted_total: u64) {
        self.session_avg_tps = Some(avg_tps);
        self.kv_reuse = if total > 0 {
            Some((cached as f64 / total as f64).clamp(0.0, 1.0))
        } else {
            None
        };
        self.spec_acceptance = if spec_draft > 0 {
            Some((spec_accepted as f64 / spec_draft as f64).clamp(0.0, 1.0))
        } else {
            None
        };
        self.tokens_total = Some(total.saturating_add(predicted_total));
    }

    /// Record the server's parallel slot count from a `/slots` poll (the
    /// `/slots` array length — how many requests can run concurrently). Only a
    /// non-zero count is stored, so a transient empty response can't blank the
    /// `×N slots` chip.
    pub fn feed_slots_count(&mut self, n: u64) {
        if n > 0 {
            self.n_slots = Some(n);
        }
    }

    /// Record a `/props` sample (once at startup). Only non-empty values are
    /// applied so a partial response can't blank a known field.
    pub fn feed_props(&mut self, model: Option<String>, quant: Option<String>, ctx_tokens: Option<u64>) {
        if let Some(m) = model {
            if !m.is_empty() {
                self.model = Some(m);
            }
        }
        if let Some(q) = quant {
            if !q.is_empty() {
                self.quant = Some(q);
            }
        }
        if let Some(c) = ctx_tokens {
            if c > 0 {
                self.ctx_tokens = Some(c);
            }
        }
    }

    /// The phase the card should render for the current state.
    pub fn phase(&self) -> Phase {
        if self.conn != ConnState::Up {
            return Phase::Down;
        }
        if !self.working {
            return Phase::Idle;
        }
        // Processing, but no tokens decoded yet → still chewing the prompt.
        if self.decoded == 0 {
            return Phase::Prompting;
        }
        Phase::Generating
    }

    /// Rough time-to-completion for the current request, if known:
    /// `n_remain / live rate`. `None` when there's no bound (`n_remain < 0`)
    /// or no rate yet (first cycle / prompting).
    pub fn eta_secs(&self) -> Option<f64> {
        if self.n_remain > 0 && self.toks_per_s > 0.0 {
            Some(self.n_remain as f64 / self.toks_per_s)
        } else {
            None
        }
    }

    /// Live context-usage ratio 0.0–1.0: `ctx_used / ctx_tokens` (clamped).
    /// `None` when the occupancy hasn't been reported yet (first poll) or
    /// the window size is unknown (no `/props` and no slot `n_ctx`).
    pub fn ctx_usage(&self) -> Option<f64> {
        match (self.ctx_used, self.ctx_tokens) {
            (Some(used), Some(total)) if total > 0 => Some((used as f64 / total as f64).clamp(0.0, 1.0)),
            _ => None,
        }
    }

    /// Adaptive poll cadence for the hot path (spec §3.3): fast while working
    /// (any sub-state that shows a live number), slower when idle, throttled
    /// when the endpoint is down. Cadence values come from the config
    /// (`PollTuning`).
    pub fn poll_interval(&self, t: PollTuning) -> Duration {
        match (self.conn, self.working) {
            (ConnState::Up, true) => Duration::from_millis(t.working_ms),
            (ConnState::Up, false) => Duration::from_millis(t.idle_ms),
            (ConnState::Down, _) => Duration::from_millis(t.error_ms),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn d(ms: u64) -> Duration {
        Duration::from_millis(ms)
    }

    /// `feed_ok` with no live context occupancy (pre-feature tests).
    fn ok(s: &mut BuddyState, working: bool, decode: u64, remain: i64, elapsed: Duration) {
        s.feed_ok(working, decode, remain, None, None, None, None, elapsed)
    }

    #[test]
    fn idle_is_healthy_and_zero() {
        let mut s = BuddyState::default();
        ok(&mut s, false, 0, -1, d(2000));
        assert_eq!(s.conn, ConnState::Up);
        assert!(!s.working);
        assert_eq!(s.toks_per_s, 0.0);
        assert_eq!(s.phase(), Phase::Idle);
        assert_eq!(s.poll_interval(PollTuning::default()), d(2000));
    }

    #[test]
    fn first_working_cycle_has_no_rate() {
        let mut s = BuddyState::default();
        ok(&mut s, false, 0, -1, d(2000)); // idle baseline
        ok(&mut s, true, 42, -1, d(500)); // transition to working
        assert!(s.working);
        assert_eq!(s.toks_per_s, 0.0); // "—" / "prompting" for one cycle
        assert_eq!(s.phase(), Phase::Generating); // decode_total > 0
    }

    #[test]
    fn prefill_is_prompting_phase() {
        let mut s = BuddyState::default();
        ok(&mut s, false, 0, -1, d(2000));
        ok(&mut s, true, 0, -1, d(500)); // processing, but 0 decoded → prefill
        assert!(s.working);
        assert_eq!(s.toks_per_s, 0.0);
        assert_eq!(s.phase(), Phase::Prompting);
    }

    #[test]
    fn prefill_resolves_to_generating_when_decode_starts() {
        let mut s = BuddyState::default();
        ok(&mut s, false, 0, -1, d(2000));
        ok(&mut s, true, 0, -1, d(500)); // prefill: processing, 0 decoded
        assert_eq!(s.phase(), Phase::Prompting);
        ok(&mut s, true, 40, -1, d(500)); // decode begins
        assert_eq!(s.phase(), Phase::Generating); // decode_total > 0
        assert_eq!(s.toks_per_s, 0.0); // first decode poll: 0 baseline → no rate yet
        ok(&mut s, true, 120, -1, d(500)); // +80 tokens in 0.5 s
        assert!((s.toks_per_s - 160.0).abs() < 1e-9);
    }

    #[test]
    fn delta_rate_over_real_elapsed() {
        let mut s = BuddyState::default();
        ok(&mut s, false, 0, -1, d(2000));
        ok(&mut s, true, 10, -1, d(500));
        ok(&mut s, true, 110, -1, d(1000)); // +100 tokens in 1.0 s
        assert!((s.toks_per_s - 100.0).abs() < 1e-9);
        assert_eq!(s.phase(), Phase::Generating);
        assert_eq!(s.poll_interval(PollTuning::default()), d(500)); // fast while working
    }

    #[test]
    fn counter_reset_clears_rate() {
        let mut s = BuddyState::default();
        ok(&mut s, true, 10, -1, d(500));
        ok(&mut s, true, 110, -1, d(500));
        assert!(s.toks_per_s > 0.0);
        ok(&mut s, true, 5, -1, d(500)); // new request, counter restarted
        assert_eq!(s.toks_per_s, 0.0);
    }

    #[test]
    fn eta_from_remain_and_rate() {
        let mut s = BuddyState::default();
        ok(&mut s, false, 0, -1, d(2000));
        ok(&mut s, true, 10, 1000, d(500));
        ok(&mut s, true, 110, 900, d(1000)); // rate ≈ 100 tok/s, 900 left
        assert!((s.toks_per_s - 100.0).abs() < 1e-9);
        let eta = s.eta_secs().unwrap();
        assert!((eta - 9.0).abs() < 1e-9);
    }

    #[test]
    fn eta_none_without_bound_or_rate() {
        let mut s = BuddyState::default();
        ok(&mut s, true, 10, -1, d(500)); // unbounded
        assert!(s.eta_secs().is_none());
    }

    #[test]
    fn error_clears_everything() {
        let mut s = BuddyState::default();
        ok(&mut s, true, 110, -1, d(500));
        s.feed_error();
        assert_eq!(s.conn, ConnState::Down);
        assert!(!s.working);
        assert_eq!(s.toks_per_s, 0.0);
        assert_eq!(s.decoded, 0);
        assert_eq!(s.phase(), Phase::Down);
        assert_eq!(s.poll_interval(PollTuning::default()), d(5000));
    }

    #[test]
    fn recovery_snaps_back_to_ok() {
        let mut s = BuddyState::default();
        s.feed_error();
        ok(&mut s, false, 0, -1, d(5000));
        assert_eq!(s.conn, ConnState::Up);
        assert_eq!(s.phase(), Phase::Idle);
        assert_eq!(s.poll_interval(PollTuning::default()), d(2000));
    }

    #[test]
    fn poll_interval_uses_config_tuning() {
        let mut s = BuddyState::default();
        // custom tuning: fast working cadence, slow idle cadence
        let t = PollTuning { working_ms: 250, idle_ms: 4000, error_ms: 7000 };
        ok(&mut s, true, 10, -1, d(250));
        ok(&mut s, true, 20, -1, d(250)); // second sample → generating
        assert_eq!(s.poll_interval(t), d(250)); // working → working_ms
        ok(&mut s, false, 0, -1, d(4000));
        assert_eq!(s.poll_interval(t), d(4000)); // idle → idle_ms
        s.feed_error();
        assert_eq!(s.poll_interval(t), d(7000)); // down → error_ms
    }

    #[test]
    fn ctx_usage_ratio_and_clamp() {
        let mut s = BuddyState::default();
        s.feed_props(None, None, Some(160_000));
        // no occupancy yet → no ratio
        assert!(s.ctx_usage().is_none());
        s.feed_ok(false, 0, -1, Some(71_883), Some(160_000), None, None, d(500));
        assert!((s.ctx_usage().unwrap() - 71_883.0 / 160_000.0).abs() < 1e-9);
        // over-100% clamps to 1.0, never negative
        s.feed_ok(false, 0, -1, Some(200_000), Some(160_000), None, None, d(500));
        assert_eq!(s.ctx_usage().unwrap(), 1.0);
    }

    #[test]
    fn ctx_usage_needs_window_size() {
        let mut s = BuddyState::default();
        // occupancy but unknown window (no /props, no slot n_ctx) → None
        s.feed_ok(false, 0, -1, Some(5_000), None, None, None, d(500));
        assert!(s.ctx_usage().is_none());
        // slot n_ctx backs up the missing /props window
        s.feed_ok(false, 0, -1, Some(5_000), Some(10_000), None, None, d(500));
        assert!((s.ctx_usage().unwrap() - 0.5).abs() < 1e-9);
        assert_eq!(s.ctx_tokens, Some(10_000));
    }

    #[test]
    fn down_clears_live_ctx_occupancy() {
        let mut s = BuddyState::default();
        s.feed_props(None, None, Some(160_000));
        s.feed_ok(false, 0, -1, Some(71_883), None, None, None, d(500));
        assert_eq!(s.ctx_used, Some(71_883));
        s.feed_error();
        assert_eq!(s.ctx_used, None);
        assert!(s.ctx_usage().is_none());
    }

    #[test]
    fn metrics_kv_reuse_ratio() {
        let mut s = BuddyState::default();
        s.feed_metrics(121.0, 92, 100, 0, 0, 0);
        assert!((s.kv_reuse.unwrap() - 0.92).abs() < 1e-9);
        assert_eq!(s.session_avg_tps, Some(121.0));
        // total == 0 → no ratio
        s.feed_metrics(0.0, 0, 0, 0, 0, 0);
        assert!(s.kv_reuse.is_none());
        assert_eq!(s.session_avg_tps, Some(0.0));
    }

    #[test]
    fn metrics_ratio_clamped() {
        let mut s = BuddyState::default();
        s.feed_metrics(10.0, 200, 100, 0, 0, 0); // cached > total (shouldn't happen)
        assert_eq!(s.kv_reuse, Some(1.0));
    }

    #[test]
    fn metrics_spec_acceptance_ratio() {
        let mut s = BuddyState::default();
        s.feed_metrics(0.0, 0, 0, 2875, 4062, 0);
        assert!((s.spec_acceptance.unwrap() - 2875.0 / 4062.0).abs() < 1e-9); // ≈ 0.71
        // no drafts → no ratio (speculative off)
        s.feed_metrics(0.0, 0, 0, 0, 0, 0);
        assert!(s.spec_acceptance.is_none());
        // accepted > draft clamps to 1.0
        s.feed_metrics(0.0, 0, 0, 200, 100, 0);
        assert_eq!(s.spec_acceptance, Some(1.0));
    }

    #[test]
    fn prefill_progress_and_rate() {
        let mut s = BuddyState::default();
        // First working cycle: prompt 54957 total, 23090 processed → ~42%.
        s.feed_ok(true, 0, -1, Some(54_957), Some(160_000), Some(23_090), Some(54_957), d(2000));
        assert_eq!(s.phase(), Phase::Prompting); // decode_total == 0
        assert!((s.prefill_frac.unwrap() - 23_090.0 / 54_957.0).abs() < 1e-9);
        assert_eq!(s.prefill_tps, 0.0); // first cycle: no baseline yet
        // Second cycle: +8500 processed in 0.5 s → 17000 tok/s.
        s.feed_ok(true, 0, -1, Some(54_957), Some(160_000), Some(31_590), Some(54_957), d(500));
        assert!((s.prefill_tps - 17_000.0).abs() < 1e-6);
        assert!((s.prefill_frac.unwrap() - 31_590.0 / 54_957.0).abs() < 1e-9);
        // Decode begins → Generating; the prefill fields persist (last known).
        s.feed_ok(true, 120, -1, Some(55_077), Some(160_000), Some(54_957), Some(54_957), d(500));
        assert_eq!(s.phase(), Phase::Generating);
        // Down clears the prefill state.
        s.feed_error();
        assert!(s.prefill_frac.is_none());
        assert_eq!(s.prefill_tps, 0.0);
    }

    #[test]
    fn props_applies_only_non_empty() {
        let mut s = BuddyState::default();
        s.feed_props(Some("Qwen3.8-27B".into()), Some("Q4_K - Medium".into()), Some(160000));
        assert_eq!(s.model.as_deref(), Some("Qwen3.8-27B"));
        assert_eq!(s.quant.as_deref(), Some("Q4_K - Medium"));
        assert_eq!(s.ctx_tokens, Some(160000));
        // empty values must not blank a known field
        s.feed_props(Some("".into()), None, Some(0));
        assert_eq!(s.model.as_deref(), Some("Qwen3.8-27B"));
        assert_eq!(s.quant.as_deref(), Some("Q4_K - Medium"));
        assert_eq!(s.ctx_tokens, Some(160000));
    }

    #[test]
    fn sparkline_accumulates_decode_rates() {
        let mut s = BuddyState::default();
        // idle first (baseline), then three advancing decode polls
        s.feed_ok(true, 0, -1, None, None, None, None, std::time::Duration::from_millis(500));
        for i in 1..=3 {
            s.feed_ok(true, (i * 100) as u64, -1, None, None, None, None, std::time::Duration::from_millis(500));
        }
        let h = s.rate_history();
        assert!(h.len() >= 2, "expected several samples, got {h:?}");
        assert!(h.iter().all(|&r| r > 0.0));
        // roughly 200 tok/s (100 tok per 0.5 s)
        assert!((h.last().unwrap() - 200.0).abs() < 5.0);
    }

    #[test]
    fn display_mode_toggles_between_data_and_scene() {
        let mut s = WidgetState {
            servers: Vec::new(),
            labels: Vec::new(),
            display_mode: DisplayMode::default(),
        };
        assert_eq!(s.display_mode, DisplayMode::Data); // default
        s.toggle_mode();
        assert_eq!(s.display_mode, DisplayMode::Scene);
        s.toggle_mode();
        assert_eq!(s.display_mode, DisplayMode::Data);
        s.set_mode(DisplayMode::Scene);
        assert_eq!(s.display_mode, DisplayMode::Scene);
    }

    #[test]
    fn down_clears_session_stats_and_sparkline() {
        let mut s = BuddyState::default();
        s.feed_metrics(121.0, 9200, 10000, 0, 0, 5000);
        s.set_rate_hist(vec![100.0, 110.0]);
        s.feed_error();
        assert!(s.session_avg_tps.is_none());
        assert!(s.kv_reuse.is_none());
        assert!(s.tokens_total.is_none());
        assert!(s.rate_history().is_empty());
    }

    #[test]
    fn metrics_tokens_total_is_prompt_plus_generated() {
        let mut s = BuddyState::default();
        s.feed_metrics(0.0, 0, 58_205, 0, 0, 6_487);
        assert_eq!(s.tokens_total, Some(58_205 + 6_487)); // session total
        // saturating: no overflow on absurd counters
        s.feed_metrics(0.0, 0, u64::MAX, 0, 0, 10);
        assert_eq!(s.tokens_total, Some(u64::MAX));
        s.feed_error();
        assert!(s.tokens_total.is_none()); // cleared on down
    }

    #[test]
    fn slots_count_stored_and_ignores_zero() {
        let mut s = BuddyState::default();
        assert!(s.n_slots.is_none());
        s.feed_slots_count(2);
        assert_eq!(s.n_slots, Some(2));
        // A transient empty / 0-slot response must not blank the chip.
        s.feed_slots_count(0);
        assert_eq!(s.n_slots, Some(2));
        // A real change still updates.
        s.feed_slots_count(4);
        assert_eq!(s.n_slots, Some(4));
    }

}