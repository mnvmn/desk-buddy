//! Background poller: one `GET /slots` per inference server on an OS thread,
//! feeding the shared `WidgetState` (one `BuddyState` per server).
//!
//! Runs off the egui thread so UI latency never blocks the network, and the
//! network never blocks the UI. The UI reads the shared
//! `Arc<Mutex<WidgetState>>` each frame; the poller calls
//! `ctx.request_repaint()` so the card updates promptly between input events.
//!
//! Three sources, per server:
//!   * `/slots`   — every poll (hot path), drives the live number.
//!   * `/props`   — at startup, then every ~60 s (best-effort), model/quant/ctx.
//!   * `/metrics` — every ~2 s (background, best-effort), fills session stats.
//! Only `/slots` failures affect that server's connectivity; the other two
//! degrading never take it down (see `buddy_state::feed_props` / `feed_metrics`).
//!
//! The server list comes from `config::Config` (a JSON file, not code) —
//! adding a server is an edit, not a recompile.

use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use crate::buddy_state::WidgetState;
use crate::config::{Config, ServerCfg};

/// How often to refresh `/metrics` session stats (background, not the hot path).
const METRICS_INTERVAL: Duration = Duration::from_millis(2000);
/// How often to re-fetch `/props` (model / quant / ctx) so a model swap is
/// picked up without restarting the widget. Fills in a minute or less.
const PROPS_INTERVAL: Duration = Duration::from_secs(60);
/// Shorter timeout for the best-effort endpoints so they never stall the hot path.
const SOFT_TIMEOUT: Duration = Duration::from_millis(1000);

/// One successful poll's payload from `GET /slots`, derived defensively
/// (spec §3.2 + API_ENDPOINTS.md): `next_token` is an **array**; take the
/// **processing** slot's `n_decoded` (parity P2 — see `fetch_slots`; not a
/// sum over all slots, or an idle slot's stale counter breaks the rate) and
/// its `n_remain`. Also carries the live context occupancy — `sum(n_prompt_tokens +
/// n_tokens)` across ALL slots, so the generated tokens count as they
/// accumulate and the last request's occupancy shows while idle (parity
/// P1/P5: the device firmware counts exactly this, so both targets must
/// read the same number from the same server). Plus the slot's `n_ctx`
/// (its total window — same number as `/props`, but fresh from the hot
/// path). Finally the **processing** slot's `n_prompt_tokens_processed` /
/// `n_prompt_tokens` (prefill progress + live prefill rate); `None` when
/// nothing is processing. Finally `n_slots` — the array length, i.e. the
/// server's parallel inference slots (drives the `×N slots` chip).
type SlotData = (bool, u64, i64, Option<u64>, Option<u64>, Option<u64>, Option<u64>, u64);
//   (working, decode_total, n_remain, ctx_used, n_ctx, prefill_processed, prefill_total, n_slots)

pub fn fetch_slots(host: &str) -> Result<SlotData, String> {
    let url = format!("http://{host}/slots");
    let body = ureq::get(&url)
        .timeout(Duration::from_millis(1500))
        .call()
        .map_err(|e| e.to_string())?
        .into_string()
        .map_err(|e| e.to_string())?;

    let v: serde_json::Value = serde_json::from_str(&body).map_err(|e| e.to_string())?;
    let slots = v.as_array().ok_or_else(|| "slots: not an array".to_string())?;

    let (mut working, mut total, mut remain) = (false, 0u64, -1i64);
    let (mut ctx_used, mut n_ctx) = (None, None);
    let (mut prefill_processed, mut prefill_total) = (None, None);
    let mut used = 0u64;
    for slot in slots {
        let processing = slot.get("is_processing").and_then(|x| x.as_bool()).unwrap_or(false);
        if processing {
            working = true;
        }
        // True occupancy: prompt tokens + tokens generated so far, summed
        // across ALL slots (the last request's prompt stays in the slot
        // while idle — parity P1).
        used += slot.get("n_prompt_tokens").and_then(|x| x.as_u64()).unwrap_or(0)
            + slot.get("n_tokens").and_then(|x| x.as_u64()).unwrap_or(0);
        if ctx_used.is_none() {
            ctx_used = Some(used);
        }
        if n_ctx.is_none() {
            n_ctx = slot.get("n_ctx").and_then(|x| x.as_u64());
        }
        // Prefill progress: from the slot that's actually processing.
        if processing && prefill_total.is_none() {
            prefill_total = slot.get("n_prompt_tokens").and_then(|x| x.as_u64());
            prefill_processed = slot.get("n_prompt_tokens_processed").and_then(|x| x.as_u64());
        }
        // Rate source (parity P2, matches firmware): the PROCESSING slot's
        // `n_decoded` only — NOT a sum over all slots. `n_decoded` is a
        // per-request counter that resets per request, so adding an idle slot's
        // stale value double-counts when >1 slot runs in parallel (a second
        // active slot read the number as 2× the real rate) and poisons the
        // Δ baseline when a finished slot holds a large value (the next
        // request's rate came out 0). The firmware uses exactly this: it walks
        // only the processing slot for its decode counter.
        if processing {
            if let Some(entries) = slot.get("next_token").and_then(|x| x.as_array()) {
                for nt in entries {
                    if let Some(d) = nt.get("n_decoded").and_then(|x| x.as_u64()) {
                        total += d;
                    }
                    if let Some(r) = nt.get("n_remain").and_then(|x| x.as_i64()) {
                        remain = r.max(-1);
                    }
                }
            }
        }
    }
    Ok((working, total, remain, ctx_used, n_ctx, prefill_processed, prefill_total, slots.len() as u64))
}

/// `(model, quant, ctx_tokens)` from `GET /props`.
///
/// Parsed defensively across llama.cpp versions: the model name may be
/// `model_alias` (new) or a top-level `name`; the context may live under
/// `default_generation_settings.n_ctx` (new) or top-level `n_ctx` (old).
/// Any missing field → `None`, never an error.
type Props = (Option<String>, Option<String>, Option<u64>);

fn basename(path: &str) -> &str {
    path.rsplit(['/', '\\']).next().unwrap_or(path)
}

/// Shorten a model basename to a display name: drop a trailing shard suffix
/// (`-00001-of-00002`) and a trailing quant token (`-Q6_K`, `-Q5_K_XL`). Both
/// are file-packaging noise — the quant is already shown on the capacity line,
/// and the shard split is meaningless to the user. Keeps the model's identity
/// (family + size) whole so the card never has to chop it with a "…".
fn clean_model_name(name: &str) -> String {
    let mut s = name.to_string();
    // Trailing shard: `...-00001-of-00002`. `rfind("-of-")` splits it; the
    // number before it and after it must both be digits for it to be a shard.
    if let Some(of) = s.rfind("-of-") {
        let before = &s[..of];
        let after = &s[of + 4..];
        if let Some(dash) = before.rfind('-') {
            let shard = &before[dash + 1..];
            if !shard.is_empty()
                && shard.chars().all(|c| c.is_ascii_digit())
                && !after.is_empty()
                && after.chars().all(|c| c.is_ascii_digit())
            {
                s.truncate(dash);
            }
        }
    }
    // Trailing quant token: starts `Q<digit>_` (Q4_K, Q5_K_XL, Q6_K, ...).
    if let Some(dash) = s.rfind('-') {
        let tail = &s[dash + 1..];
        if tail.len() >= 3
            && tail.starts_with('Q')
            && tail.as_bytes().get(1).is_some_and(u8::is_ascii_digit)
            && tail.contains('_')
        {
            s.truncate(dash);
        }
    }
    s
}

pub fn fetch_props(host: &str) -> Result<Props, String> {
    let url = format!("http://{host}/props");
    let body = ureq::get(&url)
        .timeout(SOFT_TIMEOUT)
        .call()
        .map_err(|e| e.to_string())?
        .into_string()
        .map_err(|e| e.to_string())?;

    let v: serde_json::Value = serde_json::from_str(&body).map_err(|e| e.to_string())?;

    // llama.cpp `model_alias` is often the *full file path* (e.g.
    // `H:\AI\...Q4_K_XL.gguf`), not a short name — reduce to a clean basename,
    // drop the `.gguf` extension, and shorten the packaging noise (trailing
    // `-Qn_…` quant token, already on the capacity line, and `-NNNNN-of-NNNNN`
    // shard suffix) so the card reads `Qwen3.8-27B-UD`, not a path.
    let model = v
        .get("model_alias")
        .and_then(|x| x.as_str())
        .or_else(|| v.get("name").and_then(|x| x.as_str()))
        .or_else(|| v.get("model_path").and_then(|x| x.as_str()))
        .map(basename)
        .map(|s| s.strip_suffix(".gguf").unwrap_or(s))
        .filter(|s| !s.is_empty())
        .map(clean_model_name);

    let quant = v
        .get("model_ftype")
        .and_then(|x| x.as_str())
        .filter(|s| !s.is_empty())
        .map(str::to_string);

    let ctx_tokens = v
        .get("default_generation_settings")
        .and_then(|x| x.get("n_ctx"))
        .or_else(|| v.get("n_ctx"))
        .and_then(|x| x.as_u64());

    Ok((model, quant, ctx_tokens))
}

/// `(avg_tps, prompt_cached_total, prompt_total, spec_accepted, spec_draft, predicted_total)`
/// from `GET /metrics`.
///
/// Hand-rolled Prometheus text parse: walk lines, skip `#` comments, and grab
/// the metric names we care about. Missing metric → 0.0 / 0. The two prompt
/// counters give a robust KV-cache reuse ratio; the gauge is lagging and used
/// only as a session-average footnote. The two speculative-decode counters
/// give a stable acceptance ratio (accepted / draft tokens). `predicted_total`
/// is the cumulative generation-token counter (session total).
type Metrics = (f64, u64, u64, u64, u64, u64);

fn parse_metric<'a>(text: &'a str, name: &str) -> Option<&'a str> {
    for line in text.lines() {
        if line.starts_with('#') {
            continue;
        }
        let (key, value) = line.split_once(' ')?;
        if key == name {
            return Some(value.trim());
        }
    }
    None
}

pub fn fetch_metrics(host: &str) -> Result<Metrics, String> {
    let url = format!("http://{host}/metrics");
    let body = ureq::get(&url)
        .timeout(SOFT_TIMEOUT)
        .call()
        .map_err(|e| e.to_string())?
        .into_string()
        .map_err(|e| e.to_string())?;

    let avg = parse_metric(&body, "llamacpp:predicted_tokens_seconds")
        .and_then(|v| v.parse::<f64>().ok())
        .unwrap_or(0.0);
    let cached = parse_metric(&body, "llamacpp:prompt_tokens_cached_total")
        .and_then(|v| v.parse::<f64>().ok())
        .map(|f| f as u64)
        .unwrap_or(0);
    let total = parse_metric(&body, "llamacpp:prompt_tokens_total")
        .and_then(|v| v.parse::<f64>().ok())
        .map(|f| f as u64)
        .unwrap_or(0);
    let spec_accepted = parse_metric(&body, "llamacpp:spec_decode_num_accepted_tokens_total")
        .and_then(|v| v.parse::<f64>().ok())
        .map(|f| f as u64)
        .unwrap_or(0);
    let spec_draft = parse_metric(&body, "llamacpp:spec_decode_num_draft_tokens_total")
        .and_then(|v| v.parse::<f64>().ok())
        .map(|f| f as u64)
        .unwrap_or(0);
    let predicted_total = parse_metric(&body, "llamacpp:tokens_predicted_total")
        .and_then(|v| v.parse::<f64>().ok())
        .map(|f| f as u64)
        .unwrap_or(0);

    Ok((avg, cached, total, spec_accepted, spec_draft, predicted_total))
}

/// Build the initial shared state from the config: one `BuddyState` per
/// server (in card order) + the labels.
pub fn initial_state(cfg: &Config) -> WidgetState {
    WidgetState {
        servers: (0..cfg.servers.len().max(1)).map(|i| {
            let mut s = crate::buddy_state::BuddyState::default();
            s.host_label = Some(cfg.servers[i].host.clone());
            s
        }).collect(),
        labels: cfg.servers.iter().map(|s| s.label.clone()).collect(),
        display_mode: crate::buddy_state::DisplayMode::default(),
    }
}

/// Spawn the polling thread. It polls every configured server, adapts the
/// whole thread's cadence to the fastest-needed server (so a busy server
/// tracks fast even if another is idle), and never exits.
pub fn spawn_poller(state: Arc<Mutex<WidgetState>>, ctx: eframe::egui::Context, cfg: Config) -> JoinHandle<()> {
    let servers: Vec<ServerCfg> = cfg.servers.clone();
    let poll = cfg.poll;
    thread::spawn(move || {
        let mut last = vec![Instant::now(); servers.len()];
        let mut last_metrics = vec![Instant::now(); servers.len()];
        // `/props` was just fetched at startup, so start the re-fetch timer
        // there too (first refresh lands ~60 s in, not immediately).
        let mut last_props = vec![Instant::now(); servers.len()];

        // Per-poll diagnostic log (diagnose phase transitions per server).
        // Best-effort — never blocks the poller if it can't open.
        let mut log = match std::fs::OpenOptions::new().create(true).append(true).open("widget_state.log") {
            Ok(f) => Some(std::io::BufWriter::new(f)),
            Err(_) => None,
        };
        let run_start = std::time::Instant::now();

        // Best-effort /props once at startup per server — fills model/quant/ctx.
        // Fetched WITHOUT holding the lock (no UI stall), then applied.
        {
            let mut updates: Vec<Props> = Vec::new();
            for srv in servers.iter() {
                match fetch_props(&srv.host) {
                    Ok(p) => updates.push(p),
                    Err(e) => eprintln!("[buddy] /props unavailable for {}: {e}", srv.host),
                }
            }
            let mut st = state.lock().unwrap();
            for (i, (model, quant, ctx)) in updates.into_iter().enumerate() {
                st.servers[i].feed_props(model, quant, ctx);
            }
        }

        loop {
            // 1) Fetch /slots for every server WITHOUT holding the lock, so a
            //    slow/unreachable server (1.5 s timeout) never blocks the UI.
            let mut results: Vec<Result<SlotData, String>> = Vec::new();
            for srv in servers.iter() {
                results.push(fetch_slots(&srv.host));
            }

            // 2) Apply the batch under one short lock, picking the fastest
            //    needed cadence for the whole thread.
            let mut interval = Duration::from_secs(60);
            {
                let mut st = state.lock().unwrap();
                for (i, res) in results.into_iter().enumerate() {
                    let s = &mut st.servers[i];
                    match res {
                        Ok((working, total, remain, prompt_tokens, n_ctx, prefill_processed, prefill_total, n_slots)) => {
                            s.feed_ok(working, total, remain, prompt_tokens, n_ctx, prefill_processed, prefill_total, last[i].elapsed());
                            s.feed_slots_count(n_slots);
                        }
                        Err(_) => s.feed_error(),
                    }
                    interval = interval.min(s.poll_interval(poll));
                }
            }
            for i in 0..servers.len() {
                last[i] = Instant::now();
            }

            // Per-poll diagnostic log: one line per server, per poll.
            if let Some(ref mut l) = log {
                use std::io::Write;
                if let Ok(g) = state.lock() {
                    let line: String = g
                        .servers
                        .iter()
                        .enumerate()
                        .map(|(i, s)| format!("[{:#05}] {}", i + 1, s.phase()))
                        .collect::<Vec<_>>()
                        .join(" | ");
                    let _ = writeln!(l, "{:>8}  {}", run_start.elapsed().as_secs_f32(), line);
                    let _ = l.flush();
                }
            }

            // 3) Background /metrics refresh per server, every ~2 s (best-effort).
            for i in 0..servers.len() {
                if last_metrics[i].elapsed() >= METRICS_INTERVAL {
                    last_metrics[i] = Instant::now();
                    if let Ok((avg, cached, total, spec_accepted, spec_draft, predicted_total)) = fetch_metrics(&servers[i].host) {
                        let mut st = state.lock().unwrap();
                        st.servers[i].feed_metrics(avg, cached, total, spec_accepted, spec_draft, predicted_total);
                    }
                }
            }

            // 4) Background /props refresh per server, every ~60 s (best-effort):
            //    picks up a model swap (name / quant / ctx) without a restart.
            //    `feed_props` merges non-empty-only, so a transient failure
            //    (or an all-None response) leaves the last good values in place.
            for i in 0..servers.len() {
                if last_props[i].elapsed() >= PROPS_INTERVAL {
                    last_props[i] = Instant::now();
                    if let Ok((model, quant, ctx)) = fetch_props(&servers[i].host) {
                        let mut st = state.lock().unwrap();
                        st.servers[i].feed_props(model, quant, ctx);
                    }
                }
            }

            thread::sleep(interval);
            ctx.request_repaint();
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_metric_finds_value_and_ignores_comments() {
        let text = "# HELP llamacpp:predicted_tokens_seconds gauge\n\
                    # TYPE llamacpp:predicted_tokens_seconds gauge\n\
                    llamacpp:predicted_tokens_seconds 75.0724\n\
                    llamacpp:requests_processing 0\n";
        assert_eq!(
            parse_metric(text, "llamacpp:predicted_tokens_seconds").unwrap(),
            "75.0724"
        );
        assert_eq!(parse_metric(text, "llamacpp:requests_processing").unwrap(), "0");
        assert_eq!(parse_metric(text, "llamacpp:does_not_exist"), None);
    }

    #[test]
    fn basename_handles_unix_and_win_paths() {
        assert_eq!(basename("/a/b/model.gguf"), "model.gguf");
        assert_eq!(basename("H:\\AI\\qwen\\model.gguf"), "model.gguf");
        assert_eq!(basename("model.gguf"), "model.gguf");
    }

    #[test]
    fn clean_model_name_drops_quant_and_shard() {
        // Trailing quant token dropped (already on the capacity line).
        assert_eq!(
            clean_model_name("ThinkingCap-Qwen3.8-27B-Q6_K"),
            "ThinkingCap-Qwen3.8-27B"
        );
        // Shard suffix AND quant dropped; family + size kept whole.
        assert_eq!(
            clean_model_name("Qwen3-Next-80B-A3B-Instruct-UD-Q5_K_XL-00001-of-00002"),
            "Qwen3-Next-80B-A3B-Instruct-UD"
        );
        // Quant alone (no shard).
        assert_eq!(
            clean_model_name("Qwen3.8-27B-UD-Q4_K_XL"),
            "Qwen3.8-27B-UD"
        );
        // No quant token → unchanged (a `-of-` without a digit pair is not a shard).
        assert_eq!(
            clean_model_name("My-Model-v1"),
            "My-Model-v1"
        );
        // Short/empty edge cases don't panic.
        assert_eq!(clean_model_name("Q4_K"), "Q4_K");
        assert_eq!(clean_model_name(""), "");
    }

    #[test]
    fn initial_state_has_one_per_server() {
        let cfg = crate::config::Config::default(); // two default servers
        let ws = initial_state(&cfg);
        assert_eq!(ws.servers.len(), cfg.servers.len());
        assert_eq!(ws.labels.len(), cfg.servers.len());
        assert_eq!(ws.servers.iter().all(|s| s.conn == crate::buddy_state::ConnState::Down), true);
    }

    /// Live check against the real server's /slots (skipped if it's down).
    #[test]
    fn fetch_slots_live() {
        match fetch_slots(crate::config::DEFAULT_HOST) {
            Ok((working, total, remain, prompt_tokens, n_ctx, pf, pt, n_slots)) => {
                eprintln!(
                    "LIVE /slots: working={working} decode_total={total} n_remain={remain} ctx={prompt_tokens:?}/{n_ctx:?} prefill={pf:?}/{pt:?} slots={n_slots}"
                );
                assert!(remain >= -1);
            }
            Err(e) => panic!("server unreachable: {e}"),
        }
    }

    /// Live check against the real server's /props (skipped if it's down).
    #[test]
    fn fetch_props_live() {
        match fetch_props(crate::config::DEFAULT_HOST) {
            Ok((model, quant, ctx)) => {
                eprintln!("LIVE /props: model={model:?} quant={quant:?} ctx={ctx:?}");
                assert!(model.is_some() || ctx.is_some());
            }
            Err(e) => panic!("server unreachable: {e}"),
        }
    }
}
