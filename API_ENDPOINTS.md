# DeskBuddy — llama.cpp API Endpoint Probe (live)

Companion to `PRODUCT_SPEC.md` §3. This records a **live probe of the
actual server** the device will talk to, to confirm which endpoints are
real and exactly which fields exist. Run this (or `tools/probe.sh`) before
M1 to re-verify.

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
| `GET /props` | 200 | yes | Not needed by the device. |
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
| `llamacpp:prompt_tokens_total` | counter | `505345` | Prompt tokens processed. |
| `llamacpp:prompt_tokens_cached_total` | counter | `1.11e+07` | Prompt tokens reused from cache. |

The two cumulative counters (`tokens_predicted_total` /
`tokens_predicted_seconds_total`) are an **alternate, drift-free** tok/s
source: `Δ(total tokens) / Δ(total seconds)` across two polls. Useful as a
cross-check against the `predicted_tokens_seconds` gauge, or as a fallback
if the gauge reads stale.

All gauges read `0` at probe time because nothing was generating — this is
the healthy **idle** state, not an error (spec §5: `NO_SIGNAL`).

---

## 3. Consequences for the build

1. **Primary path stays `/slots`** (works on any stock server, no flag).
   Since `--metrics` is on here, the device **may prefer the
   `predicted_tokens_seconds` gauge** for a smoother number (B10) —
   detect via `/metrics` returning `200` vs `501`, per spec §3.2.
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

---

## 4. Re-verify with one command

Save as `tools/probe.sh` (or run inline):

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
