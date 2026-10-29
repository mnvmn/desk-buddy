#!/usr/bin/env python3
"""LAN JSON endpoint for the desk-buddy e-paper TRAVEL page.

Serves ``GET /next-hour.json`` (default 0.0.0.0:8341). Both the e-paper
firmware (fw/weather_test/panel_travel.cpp ``fetch_travel``) and the web mock's
live path (web/panel_v2/travel_data.js ``fetchTravelLive``) GET this and parse
the SAME fixed shape::

    {"stop","platform","now","day","lines":[
        {"line","dest","plat","dep":[{"t","d","live"}]}
    ]}

where per departure: ``t`` = "HH:MM" (scheduled time), ``d`` = live-scheduled in
seconds (0 on-time / >0 late), ``live`` = true when the imhd feed tracks the
real vehicle. The panel shows ``t``, counts down on the ACTUAL time (t+d), and
marks late (d>=45) / live / scheduled.

Data fusion (the reason this daemon exists):
  * imhd.sk real-time feed  -> the near term (~45 min), where vehicles are
    actually approaching and times can be corrected (live, late).
  * IDS BK GTFS timetable   -> the full hour, filling the far term the live
    feed can't reach (scheduled, on-time).
Both are reused from imhd_realtime.py / gtfs_next_hour.py (stdlib only).

The payload is computed in a background thread every REFRESH_SEC and served
from cache, so a GET returns in <10 ms (the web's live fetch has a 2.5 s
timeout; a blocking imhd poll can take ~3 s and would otherwise time it out).

Usage:
  python serve_next_hour.py                    # serve on 0.0.0.0:8341
  python serve_next_hour.py --port 9000        # custom port
  python serve_next_hour.py --once             # print one payload, exit (test)
"""
import argparse
import datetime as dt
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gtfs_next_hour as gnh
import imhd_realtime as imhd
from travel_config import TARGETS

# ---- the three lines, in panel order (32 | 49 | 44) -----------------------
LINE_ORDER = ["32", "49", "44"]
# Display labels: UPPERCASE, diacritics folded, no X/Z (the firmware 5x7 font
# is missing those glyphs). dest is what the panel prints next to the number.
LINE_META = {
    "32": {"dest": "DLHE DIELY", "plat": "313"},
    "49": {"dest": "KRAMARE NUSCH", "plat": "313"},
    "44": {"dest": "KOLIBA", "plat": "315"},
}
STOP = "KARPATSKA"
STOP_PLAT = "313"
MAX_DEP = 4            # rows that fit a line column on the 800x272 panel
LIVE_HORIZON = 45      # imhd real-time reach (minutes)
FULL_HORIZON = 60      # GTFS fill horizon (minutes)
REFRESH_SEC = 60       # background cache refresh cadence
PORT = 8341            # default LAN port (matches travel_data.js TRAVEL_URL)


def _hhmm(ms):
    return dt.datetime.fromtimestamp(ms / 1000).strftime("%H:%M")


def _sched():
    """GTFS scheduled departures for the full hour -> {line: ["HH:MM", ...]}."""
    out = {L: [] for L in LINE_ORDER}
    path = gnh.ensure_gtfs(max_age_h=24)
    g = gnh.load_gtfs(path)
    for r in gnh.next_hour(g, when=dt.datetime.now(), horizon_min=FULL_HORIZON, targets=TARGETS):
        L = str(r["line"])
        if L in out:
            out[L].append(r["hhmm"])
    for L in out:
        out[L].sort()
    return out


def _live():
    """imhd live departures (near term) -> {line: [{"t","d","live"}, ...]}."""
    out = {L: [] for L in LINE_ORDER}
    tables = imhd.fetch_departures()
    deps = imhd.flatten(tables)
    dests = [TARGETS[L]["imhd_dest"] for L in LINE_ORDER]
    for d in imhd.select(deps, lines=LINE_ORDER, horizon_min=LIVE_HORIZON, dests=dests):
        L = str(d.get("linka"))
        if L not in out:
            continue
        cas, casCP = d.get("cas"), d.get("casCP")
        t_ms = casCP if casCP else cas
        if not t_ms:
            continue
        if cas and casCP:
            delta = int(round((cas - casCP) / 1000.0))
        elif isinstance(d.get("casDelta"), int) and d.get("casDelta"):
            delta = d["casDelta"]
        else:
            delta = 0
        out[L].append({"t": _hhmm(t_ms), "d": delta, "live": d.get("typ") == "online"})
    return out


def build_payload():
    """One merged next-hour payload (imhd live near-term + GTFS far-term)."""
    now = dt.datetime.now()
    try:
        sched = _sched()
    except Exception as e:
        print(f"  gtfs: unavailable ({e})", file=sys.stderr)
        sched = {L: [] for L in LINE_ORDER}
    try:
        live = _live()
    except Exception as e:
        print(f"  imhd: unavailable ({e})", file=sys.stderr)
        live = {L: [] for L in LINE_ORDER}

    lines = []
    for L in LINE_ORDER:
        merged = list(live.get(L, []))
        last_live = max((x["t"] for x in merged), default=None)
        # fill the far term with GTFS times beyond the last live time
        for hh in sched.get(L, []):
            if last_live is None or hh > last_live:
                merged.append({"t": hh, "d": 0, "live": False})
        merged.sort(key=lambda x: x["t"])
        # de-dupe by time, keeping the live version over a scheduled one
        by_t = {}
        for x in merged:
            e = by_t.get(x["t"])
            if e is None or (not e["live"] and x["live"]):
                by_t[x["t"]] = x
        dep = [by_t[t] for t in sorted(by_t)][:MAX_DEP]
        meta = LINE_META[L]
        lines.append({"line": L, "dest": meta["dest"], "plat": meta["plat"], "dep": dep})

    return {
        "stop": STOP, "platform": STOP_PLAT,
        "now": now.strftime("%H:%M"), "day": now.strftime("%Y-%m-%d"),
        "lines": lines,
    }


class Cache:
    """Background-refreshed payload, served instantly on GET (thread-safe)."""

    def __init__(self, interval=REFRESH_SEC):
        self.interval = interval
        self.data = None
        self.ts = 0.0
        self._lock = threading.Lock()

    def _refresh(self):
        t0 = time.time()
        try:
            p = build_payload()
            with self._lock:
                self.data, self.ts = p, time.time()
            print(f"  refresh ok in {time.time()-t0:.1f}s  now={p['now']} "
                  f"deps={[len(l['dep']) for l in p['lines']]}", file=sys.stderr)
        except Exception as e:
            print(f"  refresh FAIL ({e})", file=sys.stderr)

    def start(self):
        self._refresh()  # prime the cache before serving
        threading.Thread(target=self._loop, daemon=True).start()

    def _loop(self):
        while True:
            time.sleep(self.interval)
            self._refresh()

    def get(self):
        with self._lock:
            return self.data, self.ts


def make_handler(cache):
    class Handler(BaseHTTPRequestHandler):
        def _send(self, code, body, ctype="application/json"):
            data = body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path in ("/next-hour.json", "/next-hour", "/"):
                p, ts = cache.get()
                if p is None:
                    self._send(503, json.dumps({"error": "not ready"}))
                    return
                self._send(200, json.dumps(p, ensure_ascii=False))
            elif path == "/health":
                p, ts = cache.get()
                self._send(200, json.dumps({"ok": p is not None, "age_s": round(time.time() - ts, 1)}))
            else:
                self._send(404, json.dumps({"error": "not found"}))

        def log_message(self, *a):
            pass  # quiet
    return Handler


def main():
    ap = argparse.ArgumentParser(description="e-paper travel JSON endpoint")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--refresh", type=int, default=REFRESH_SEC, help="cache refresh seconds")
    ap.add_argument("--once", action="store_true", help="print one payload and exit")
    a = ap.parse_args()

    if a.once:
        print(json.dumps(build_payload(), ensure_ascii=False, indent=2))
        return

    cache = Cache(interval=a.refresh)
    cache.start()
    srv = ThreadingHTTPServer(("0.0.0.0", a.port), make_handler(cache))
    import socket
    ip = socket.gethostbyname(socket.gethostname())
    print(f"travel endpoint on 0.0.0.0:{a.port} (LAN {ip}:{a.port}) — "
          f"GET /next-hour.json (refresh every {a.refresh}s)", file=sys.stderr)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
