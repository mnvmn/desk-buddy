# Module: `poller`

Adaptive multi-server HTTP client with **three endpoints, one background
thread** ([`widget/src/poller.rs`](../../../widget/src/poller.rs)). One
thread polls **every configured server** (the list comes from the
[config file](config.md)); each server gets its own `BuddyState`
(`WidgetState.servers[i]`):

| Endpoint | Cadence | Drives | Timeout |
|---|---|---|---|
| `GET /slots` | every poll per server (hot path) | `Phase` (working, decode, n_remain → live rate) + live context occupancy (`n_prompt_tokens` / `n_ctx`) | 1500 ms |
| `GET /props` | **once** at startup per server | model / quant / ctx | 1000 ms |
| `GET /metrics` | every ~2 s per server (background) | session avg tok/s, KV-cache reuse | 1000 ms |

Only `/slots` failures change that server's `conn` → `Phase::Down`; the other
two are best-effort and a failure just leaves their fields `None`.

## Responsibilities (widget impl)

- **Background OS thread** (not the egui thread) so network latency never
  blocks the UI and vice versa. Started once from `BuddyApp::new` via
  `spawn_poller(state, ctx, config)`.
- **Fetch-without-lock:** each cycle first fetches `/slots` for **every
  server with NO lock held** (a slow/unreachable server's 1.5 s timeout
  never blocks the UI), then applies the whole batch **under one short lock**
  (`feed_ok` / `feed_error` per server) and picks the **minimum** interval
  across servers so a busy server tracks fast even if another is idle.
  `ctx.request_repaint()` per cycle.
- `fetch_slots` is **defensive**: `next_token` is an **array**; it takes the
  **processing** slot's `n_decoded` (parity P2 — NOT a sum over all slots, or an
  idle slot's stale counter double-counts when >1 slot runs in parallel and
  poisons the next request's rate) and its `n_remain`, ORs `is_processing`, and
  also reads the first slot's
  `n_prompt_tokens` (live context occupancy) and `n_ctx` (backs up the
  `/props` window). Any step failing (timeout, refused, 4xx/5xx,
  parse error, "not an array") returns `Err` → that server's `Down`, never a
  crash.
- `fetch_props` is **defensive across llama.cpp versions**: model name is
  `model_alias` (new) else `name` else `model_path` basename; ctx is
  `default_generation_settings.n_ctx` (new) else top-level `n_ctx`; quant is
  `model_ftype`. Any missing field → `None`, never an error.
- `fetch_metrics` **hand-parses the Prometheus text** (`parse_metric`):
  skips `#` comment lines and grabs `llamacpp:predicted_tokens_seconds`,
  `prompt_tokens_cached_total`, `prompt_tokens_total`. Missing metric →
  0 / 0 (the state layer then derives `kv_reuse` from the two cumulative
  counters, which are robust; the gauge is lagging).
- **Servers come from the config file** (`Config::load`), so tests can point
  at [tools/mock_server.py](../../../tools/mock_server.py) without
  recompiling (`DESKBUDDY_CONFIG` → a temp config).
- **Diagnostic logging:** appends one line per poll to `widget_state.log`
  (`<t>  [00001] generating | [00002] idle` — one phase token per server),
  flushed per line, so the live multi-server path is verifiable without
  watching the window. Best-effort — a missing file never blocks the poller.

## Cadence (implemented, from `poll_interval(tuning)` — the `/slots` hot path)

| State | Interval (default) | Why |
|---|---|---|
| Working (`Up`, `working`) — incl. `Prompting` | `poll.working_ms` = 500 ms | live feel |
| Idle (`Up`, `!working`) | `poll.idle_ms` = 2 s | nothing to watch |
| Down | `poll.error_ms` = 5 s | stop hammering a dead endpoint |

The values are the `PollTuning` from the config file; the thread sleeps the
**minimum** across all servers. On the first 200 after an error, the state
snaps back to `Ok` immediately (baseline cleared, so the next working cycle
shows `—` once).

> `/metrics` **is** polled, but on a slow ~2 s background pass and only for
> session stats (footnote) — never the live number. It lags (see
> [architecture.md](../../shared/architecture.md)); the `/slots`-derived rate is the
> headline.

## Dependencies

- **Used by:** [render](render.md) (owns the thread handle, reads state)
- **Uses:** [buddy-state](buddy-state.md) (feeds `WidgetState`),
  [config](config.md) (server list + cadence), `ureq` (blocking HTTP,
  1500 ms hot-path / 1000 ms soft timeout), `serde_json`,
  `std::sync`/`std::thread`.

## Live tests (in suite)

- `live_tests::fetch_slots_live` hits the real `m.tower1:11444`/`slots` and
  asserts a parseable `n_remain`; it panics (fail-fast) if the server is
  unreachable, so run the suite only when the LAN server is up.
- `fetch_props_live` does the same against `/props` (asserts model or ctx
  resolved). For hermetic runs, point them at the mock via a config whose
  `host` is `127.0.0.1:11445` (`DESKBUDDY_CONFIG`).

## Gotchas

- Parse **defensively** — large `params` block, array-shaped `next_token`.
  Never assume nesting; on failure degrade to that server's `Down`, don't
  crash.
- **Never hold the state lock during network I/O** — a 1.5 s timeout on one
  server would stall the UI on a 3-server card. Fetch first, apply under one
  short lock.
- The hot path never depends on the slow endpoints: a `/props` or
  `/metrics` failure must not touch `conn` or the rate.
- One request per server per cycle; the thread sleeps the adaptive interval —
  no busy loop.
- The per-poll log line is diagnostic; don't make product behavior depend on
  the file existing.

See [shared/API_ENDPOINTS.md](../../../shared/API_ENDPOINTS.md) for the
endpoint/field contract.
