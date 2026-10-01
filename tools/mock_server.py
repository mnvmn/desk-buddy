#!/usr/bin/env python3
"""
DeskBuddy mock llama-server for deterministic widget testing.

Serves the SAME response shapes as a real llama-server (/slots + /metrics +
/props) but with a controllable virtual generator that advances in REAL TIME
(in a background ticker), so tests don't depend on the real server's state or
on ambient generation.

Modes:
  steady [tok/s]            generate forever at a constant rate (default 95)
  cycle [busy idle tok/s]   alternate busy/idle (default 4s busy, 2s idle, 95)
  idle                      never generates
  fail                      /slots returns 500 (tests the Down phase)

Optional: --port N (default 11445), --no-props (return 404 for /props).

The busy period of `cycle`/`steady` starts with a short PREFILL phase:
is_processing=true but n_decoded=0 (the prompt is being processed before
tokens stream). The widget renders this as the "prompting" phase.

Also serves:
  /health  -> 200 ok
  /metrics -> Prometheus text; predicted_tokens_seconds reproduces the REAL
              server's lag (0.0 while generating, value shown after) so the
              gauge's lag is reproducible in tests. Also serves the
              prompt_tokens_cached_total / prompt_tokens_total counters the
              widget uses for the "N% cached" KV-cache reuse footnote.
  /props   -> JSON; the NEW llama.cpp shape (model_alias / model_ftype /
              default_generation_settings.n_ctx). The widget falls back to the
              old top-level shape if those are absent.

Usage:
  python tools/mock_server.py cycle 4 2 95 --port 11445
  python tools/mock_server.py steady 120
  python tools/mock_server.py idle
  python tools/mock_server.py fail

Then point the widget at it (no recompile):
  DESKBuddy_HOST=127.0.0.1:11445 cargo run
"""
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class VirtualGen:
    """Produces tokens at a fixed rate while busy; the background ticker
    advances it in real time. The busy period starts with a prefill window
    (is_processing=true, n_decoded=0), then decode. n_decoded resets each
    busy period (mirrors the real server's per-request counter)."""

    def __init__(self, mode: str, busy_s: float, idle_s: float, toks_per_s: float,
                 prefill_s: float = 1.0):
        self.mode = mode
        self.busy_s = busy_s
        self.idle_s = idle_s
        self.rate = toks_per_s
        self.prefill_s = prefill_s
        self.busy = mode in ("steady", "cycle")
        self.tokens = 0.0
        self.period_start = time.monotonic()
        self.last_rate = 0.0
        self.lock = threading.Lock()

    def _tick_locked(self) -> None:
        now = time.monotonic()
        elapsed = now - self.period_start
        if self.busy:
            # Decode tokens accrue only after the prefill window; absolute,
            # not a per-tick increment, so it stays correct regardless of the
            # tick cadence.
            self.tokens = max(0.0, elapsed - self.prefill_s) * self.rate
            if self.mode == "cycle" and elapsed >= self.busy_s:
                self.last_rate = self.rate
                self.busy = False
                self.tokens = 0.0
                self.period_start = now
        else:
            if self.mode == "cycle" and elapsed >= self.idle_s:
                self.busy = True
                self.tokens = 0.0
                self.period_start = now

    def start(self) -> None:
        """Advance the virtual clock every 100 ms, independent of polls."""
        def loop():
            while True:
                with self.lock:
                    self._tick_locked()
                time.sleep(0.1)
        threading.Thread(target=loop, daemon=True).start()

    def snapshot(self):
        with self.lock:
            now = time.monotonic()
            elapsed = now - self.period_start
            if self.busy and elapsed < self.prefill_s:
                phase = "prefill"
            elif self.busy:
                phase = "decode"
            else:
                phase = "idle"
            return phase, int(self.tokens), self.last_rate

    def slots_body(self) -> str:
        phase, n, _ = self.snapshot()
        busy = phase in ("prefill", "decode")
        return json.dumps([{
            "id": 0,
            "n_ctx": 160000,
            "is_processing": busy,
            "next_token": [{
                "has_next_token": busy,
                "has_new_line": False,
                "n_remain": -1,
                "n_decoded": n,
            }],
        }])

    def metrics_body(self) -> str:
        phase, _, last_rate = self.snapshot()
        gauge = 0.0 if phase in ("prefill", "decode") else last_rate
        # reproduces the real gauge's lag
        # prompt token counters: fixed realistic values → "87% cached"
        return (
            "# HELP llamacpp:predicted_tokens_seconds Average generation throughput in tokens/s\n"
            "# TYPE llamacpp:predicted_tokens_seconds gauge\n"
            f"llamacpp:predicted_tokens_seconds {gauge}\n"
            "# HELP llamacpp:requests_processing Number of requests processing\n"
            "# TYPE llamacpp:requests_processing gauge\n"
            f"llamacpp:requests_processing {1 if phase in ('prefill', 'decode') else 0}\n"
            "# HELP llamacpp:prompt_tokens_seconds Prompt preprocessing throughput in tokens/s\n"
            "# TYPE llamacpp:prompt_tokens_seconds gauge\n"
            "llamacpp:prompt_tokens_seconds 0.0\n"
            "# HELP llamacpp:prompt_tokens_total Prompt tokens processed\n"
            "# TYPE llamacpp:prompt_tokens_total counter\n"
            "llamacpp:prompt_tokens_total 123456\n"
            "# HELP llamacpp:prompt_tokens_cached_total Prompt tokens reused from cache\n"
            "# TYPE llamacpp:prompt_tokens_cached_total counter\n"
            "llamacpp:prompt_tokens_cached_total 107407\n"
        )

    @staticmethod
    def props_body() -> str:
        # The NEW llama.cpp /props shape (model_alias + model_ftype +
        # default_generation_settings.n_ctx).
        return json.dumps({
            "model_alias": "mock-llama-8b",
            "model_ftype": "Q4_K_M",
            "model_path": "/models/mock-llama-8b.Q4_K_M.gguf",
            "total_slots": 1,
            "is_sleeping": False,
            "build_info": "b0-mock",
            "default_generation_settings": {"n_ctx": 160000},
        })


GEN: "VirtualGen" = None
NO_PROPS = False


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        if self.path == "/slots":
            if GEN.mode == "fail":
                self.send_response(500)
                self.end_headers()
                self.wfile.write(b"mock: failure mode")
                return
            body = GEN.slots_body().encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/metrics":
            body = GEN.metrics_body().encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/props":
            if NO_PROPS or GEN.mode == "fail":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(b"mock: no props")
                return
            body = GEN.props_body().encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/health":
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"ok")
        else:
            self.send_response(404)
            self.end_headers()


def status_printer(gen: VirtualGen, stop: threading.Event) -> None:
    while not stop.is_set():
        stop.wait(1.0)
        phase, n, _ = gen.snapshot()
        print(f"[mock] {phase}  n_decoded={n}", file=sys.stderr, flush=True)


def main() -> None:
    global GEN, NO_PROPS
    argv = sys.argv[1:]

    # optional flags
    port = 11445
    if "--port" in argv:
        i = argv.index("--port")
        port = int(argv[i + 1])
        del argv[i:i + 2]
    if "--no-props" in argv:
        NO_PROPS = True
        del argv[argv.index("--no-props")]

    mode = argv[0] if argv else "cycle"
    if mode == "steady":
        rate = float(argv[1]) if len(argv) > 1 else 95.0
        gen = VirtualGen(mode, 1e18, 1e18, rate)
    elif mode == "cycle":
        busy = float(argv[1]) if len(argv) > 1 else 4.0
        idle = float(argv[2]) if len(argv) > 2 else 2.0
        rate = float(argv[3]) if len(argv) > 3 else 95.0
        gen = VirtualGen(mode, busy, idle, rate)
    elif mode == "idle":
        gen = VirtualGen("idle", 1e18, 1e18, 0.0)
    elif mode == "fail":
        gen = VirtualGen("fail", 1e18, 1e18, 0.0)
    else:
        print(f"unknown mode: {mode}", file=sys.stderr)
        sys.exit(2)

    GEN = gen
    gen.start()
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    stop = threading.Event()
    threading.Thread(target=status_printer, args=(gen, stop), daemon=True).start()
    print(f"[mock] serving on http://127.0.0.1:{port}  mode={mode} props={'off' if NO_PROPS else 'on'}",
          file=sys.stderr, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()


if __name__ == "__main__":
    main()
