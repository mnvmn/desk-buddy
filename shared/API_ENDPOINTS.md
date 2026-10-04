# DeskBuddy — llama.cpp API Endpoint Probe (live)

Companion to [`esp32c6_480x480/PRODUCT_SPEC.md`](../esp32c6_480x480/PRODUCT_SPEC.md) §3. This
records a **live probe of the actual server** the device will talk to, to
confirm which endpoints are real and exactly which fields exist. Run this
(or `shared/tools/probe.sh`) before M1 to re-verify.

- **Probed:** 2026-09-14
- **Target:** `http://m.tower1:11444`
- **How:** `curl` against each endpoint from the host.
- **Status:** all expected endpoints returned `200` (see table).

> Re-run any time the server version or launch flags change — field names
> and availability can drift. The `probe.sh` script below does it in one
> command.

---

## 1. Endpoint availability (measured)

| Endpoint | HTTP | On by default? | Verdict for the device |
|---|---|---|---|
| `GET /health` | 200 | yes | Cheap liveness check (optional). |
| `GET /slots` | 200 | **yes** | **Primary path.** Always available; derive `working` + tok/s. |
| `GET /metrics` | 200 | only with `--metrics` | **Confirmed ON on this server.** Ready-made tok/s gauge (B10). |
| `GET /props` | 200 | yes | **Widget uses this once at startup** — model name, quant, ctx (§2.3). |
| `GET /model` | 404 | — | Singular form absent; ignore. (Use `/props` if a model name is ever wanted.) |

**Key finding:** `--metrics` **is on** for this server, so `GET /metrics`
returns `200`. This answers **Open Question #3** in the spec and makes
**B10 worth building** — the device can prefer the gauge for a smoother
tok/s while keeping `/slots` as the always-available fallback.

---

## 2. Fields the device reads (exact, from live responses)

### 2.1 `GET /slots`

Returns a JSON **array** of slots. Observed shape (abridged — the `params`
block is large and not needed by the device; parse defensively):

```json
[{
  "id": 0,
  "n_ctx": 160000,
  "is_processing": false,
  "n_prompt_tokens": 71883,
  "n_prompt_tokens_processed": 0,
  "n_prompt_tokens_cache": 0,
  "next_token": [
    { "has_next_token": false, "has_new_line": false, "n_remain": -1, "n_decoded": 0 }
  ]
}]
```

**Fields the device uses** (per §3.2):

| Field | Type | Use |
|---|---|---|
| `id` | int | Slot index. |
| `is_processing` | bool | **Work/idle signal.** `working = any(slot.is_processing)`. |
| `next_token[].n_decoded` | int | Tokens decoded for this slot; monotonically increasing → derive tok/s via Δ over real elapsed. |
| `next_token[].n_remain` | int | Remaining tokens (`-1` = unbounded). Optional context line. |
| `n_prompt_tokens` | int | Tokens **currently held in the context** (whole state). Live context usage = `n_prompt_tokens / n_ctx` (observed 2026-09-20: 71883 / 160000 ≈ 45%). The widget's capacity line. |
| `n_ctx` | int | The slot's context window — backs up `/props` `n_ctx` for the usage %. |
| `n_prompt_tokens_processed` / `n_prompt_tokens_cache` | int | How much of the prompt is processed / cache-hit. The widget's **prompting headline** = `n_prompt_tokens_processed / n_prompt_tokens` (live prefill %), and **prefill tok/s** = `Δ n_prompt_tokens_processed / Δ elapsed` across working polls. `_cache` is not rendered yet. |

> Note: `next_token` is an **array** (spec §3.2 describes it as a single
> object). When multiple entries exist, sum `n_decoded` across the array
> per slot and take the busy slot's `n_remain`. Parse defensively — a
> schema drift here must degrade to `OFFLINE_SERVER`, not crash (B9).

### 2.2 `GET /metrics`

Prometheus text format. **Relevant gauges/counters** (observed):

| Metric | Type | Value at probe (idle) | Use |
|---|---|---|---|
| `llamacpp:predicted_tokens_seconds` | gauge | `0` | **Ready-made generation tok/s** — the B10 gauge. |
| `llamacpp:requests_processing` | gauge | `0` | Alternate work/idle signal (0 = idle). |
| `llamacpp:prompt_tokens_seconds` | gauge | `0` | Prompt (preprocessing) throughput — not shown in MVP. |
| `llamacpp:tokens_predicted_total` | counter | `213148` | Total generation tokens (cumulative). |
| `llamacpp:tokens_predicted_seconds_total` | counter | `2548.52` | Total generation time (cumulative). |
| `llamacpp:prompt_tokens_total` | counter | `505345` | Prompt tokens processed. With `tokens_predicted_total` above, their sum → the widget's `NNk total` session footer chip (per-server, resets on restart). |
| `llamacpp:prompt_tokens_cached_total` | counter | `1.11e+07` | Prompt tokens reused from cache. |
| `llamacpp:spec_decode_num_accepted_tokens_total` | counter | — | Speculative-decode tokens **accepted**. With the draft counter below, `accepted / draft` → the widget's `spec NN%` footnote chip. |
| `llamacpp:spec_decode_num_draft_tokens_total` | counter | — | Speculative-decode tokens **drafted** (proposed). Present only when a draft model is loaded; both 0 → no `spec` chip. |

The two cumulative counters (`tokens_predicted_total` /
`tokens_predicted_seconds_total`) are an **alternate, drift-free** tok/s
source: `Δ(total tokens) / Δ(total seconds)` across two polls. Useful as a
cross-check against the `predicted_tokens_seconds` gauge, or as a fallback
if the gauge reads stale.

All gauges read `0` at probe time because nothing was generating — this is
the healthy **idle** state, not an error (spec §5: `NO_SIGNAL`).

> **Live test (2026-09-19):** while a real 800-token generation ran,
> `predicted_tokens_seconds` stayed at **0.0 the entire time** and only
> showed **127.4** *after* the request finished. It is a **lagging /
> session-average** signal, not a live one. The **derived `/slots` value**
> tracked generation live at ~126–139 tok/s. Do **not** use the gauge as
> the primary display number; use it only as a session-average cross-check.

### 2.3 `GET /props` (widget, fetched once at startup)

A JSON **object** (not an array). On this server (llama.cpp b11026) the
fields the widget wants are **nested**, not top-level — parse both shapes
defensively across versions:

| Field | Where | Use |
|---|---|---|
| `model_alias` (new) / `name` (old) | top-level | Model name line. |
| `model_ftype` | top-level | Quantization, e.g. `"Q4_K - Medium"`. |
| `default_generation_settings.n_ctx` (new) / `n_ctx` (old) | nested / top | Context window (rendered `160k`). |
| `model_path` | top-level | Fallback: basename is the model name. |

Observed live (2026-09-20): `model_alias` = `H:\AI\qwen38\Qwen3.8-27B-UD-Q4_K_XL.gguf`,
`model_ftype` = `Q4_K - Medium`, `default_generation_settings.n_ctx` = `160000`.

**The widget treats `/props` as best-effort:** it's fetched **once** on the
poller thread at startup with a short timeout; any failure (404/timeout)
leaves the model/quant/ctx fields empty and the card still works. It is
**never** part of the hot path and **never** affects `conn`.

---

## 3. Consequences for the build

1. **Primary and only display path: derived `/slots` value.**
   `tok/s = Δ sum(next_token[].n_decoded) / Δ real elapsed`, computed only
   while `working`. This tracks generation **live** (verified: 126–139 tok/s
   during a real 800-token run). It works on any stock server, no flag.
   **Do not prefer the `/metrics` gauge for the display** — it is a lagging
   session-average (verified to read 0 during generation, value after).
   B10 (gauge path) is demoted to an *optional cross-check / session avg*.
2. **Default host in device config** → `m.tower1` (or its LAN IP), port
   `11444`. **Do not** default to `127.0.0.1` (spec §3.1 — the ESP32
   cannot reach the host's loopback).
3. **LAN binding** must be verified: `llama-server` needs `--host 0.0.0.0`
   (or the specific LAN IP), not loopback-only, or the device can't connect.
   Reachable by hostname from this host is *not* the same as reachable from
   the LAN — confirm with the probe script run from a second machine, or
   inspect the server's bind address.
4. **Parse defensively.** `next_token` is an array; `params` is large.
   Never assume the exact nesting — on any parse failure degrade to
   `OFFLINE_SERVER` and retry (B9).
5. **Widget secondary endpoints are best-effort (M2).** `/props` is fetched
   **once** at startup (model name, quant, ctx) and `/metrics` is fetched on
   a slow ~2 s background pass (session-average tok/s footnote + KV-cache
   reuse from the `prompt_tokens_*_total` counters). Neither is on the hot
   path and neither can take the card down: a 404/timeout/parse failure just
   leaves those fields empty. `/slots` remains the **only** source that can
   set `conn`. The **prompting** phase is derived from `/slots` alone
   (`is_processing` true + `n_decoded == 0`), not from `/metrics`.

---

## 4. Re-verify with one command

Save as `shared/tools/probe.sh` (or run inline):

```bash
#!/usr/bin/env bash
# Usage: ./probe.sh [host:port]   (default m.tower1:11444)
B="http://${1:-m.tower1:11444}"
echo "== endpoint status =="
for ep in health slots metrics props; do
  printf 'GET /%-6s -> %s\n' "$ep" "$(curl -s -o /dev/null -w '%{http_code}' --max-time 4 "$B/$ep")"
done
echo
echo "== /slots: is_processing / n_decoded / n_remain =="
curl -s --max-time 4 "$B/slots" | grep -oE '"(is_processing|n_decoded|n_remain)":[^,}]*'
echo
echo "== /metrics: relevant gauges =="
curl -s --max-time 4 "$B/metrics" \
  | grep -E '^(llamacpp:predicted_tokens_seconds|llamacpp:prompt_tokens_seconds|llamacpp:requests_processing|llamacpp:tokens_predicted_total|llamacpp:tokens_predicted_seconds_total) '
```

**Reading the output for device health:**
- `/slots` → 200 and parses = `OK` (or `NO_SIGNAL` if `is_processing` all
  false).
- `/slots` → 4xx/5xx / parse fail / timeout = `OFFLINE_SERVER`.
- Wi-Fi itself down = `OFFLINE_WIFI` (detected before any HTTP).
- tok/s (live) = `Δ sum(next_token[].n_decoded) / Δ real elapsed`, or read
  `llamacpp:predicted_tokens_seconds` directly when `/metrics` is 200.
