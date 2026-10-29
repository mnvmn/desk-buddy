#!/usr/bin/env python3
"""Fetch LIVE next departures from an imhd.sk stop (real-time feed).

imhd.sk's "virtual stop display" talks to a Socket.IO v4 (engine.io v4)
server over HTTP long-polling at ``https://imhd.sk/rt/sio2/``. This script
reproduces that small handshake (no websocket, plain HTTP, stdlib only) and
reads the stop's departure table.

Protocol (verified against the live server, see docs/api-research.md):
  1. GET  ?EIO=4&transport=polling          -> 0{"sid":...}           (open)
  2. POST ...&sid=SID   body "40"            ->                        (socket.io CONNECT "/")
  3. GET  ...&sid=SID                       -> 40{"sid":...} 42["cack",true]
  4. POST ...&sid=SID   body 42["tabStart",[STOP,["*"]]]               (subscribe)
  5. GET  ...&sid=SID                       -> 42["vInfo",...] 42["tabs",[ {nastupiste, tab:[...]} , ... ]]

Packets in one HTTP read are separated by the byte 0x1e. Each socket.io
EVENT is ``42["name", payload]``. The event we want is ``tabs``: a list with
one entry per nástupiste (platform), each carrying ``tab`` (the departures).

Usage:
  python imhd_realtime.py                 # all lines at Karpatská, next 60 min
  python imhd_realtime.py --lines 32,49   # only the two target lines
  python imhd_realtime.py --horizon 30    # only the next 30 minutes
  python imhd_realtime.py --stop 129 --json
"""
import argparse
import datetime
import json
import re
import sys
import time
import urllib.request

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
from travel_config import IMHD_STOP_ID, IMHD_RT_BASE, TARGETS, USER_AGENT

_BASE = IMHD_RT_BASE
_HDRS = {"User-Agent": USER_AGENT, "Accept-Encoding": "identity"}


def _http(url, data=None, method=None, timeout=30):
    h = dict(_HDRS)
    if data is not None:
        h["Content-Type"] = "text/plain;charset=UTF-8"
    req = urllib.request.Request(url, headers=h, data=data, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def _read_msgs(url):
    """One long-poll GET; returns the list of engine.io messages (split on 0x1e)."""
    body = _http(url)
    return [m for m in body.split(b"\x1e") if m]


def fetch_departures(stop_id=IMHD_STOP_ID, timeout=20.0):
    """Return the list of per-platform tables for ``stop_id``.

    Each table is a dict: {"zastavka", "nastupiste", "tab": [departure, ...]}.
    A departure dict carries: linka, cas (ms, live), casCP (ms, scheduled),
    typ ("online"|"cp"), cielStr, konecnaZstr, odjazd, issi, ...
    """
    sid = re.search(rb'"sid":"([^"]+)"', _http(_BASE + "?EIO=4&transport=polling")).group(1).decode()
    url = f"{_BASE}?EIO=4&transport=polling&sid={sid}"
    deadline = time.time() + timeout

    def post(data):
        _http(url, data=data, method="POST")

    # 2) connect the socket.io namespace (engine.io MESSAGE '4' + socket.io CONNECT '0')
    post(b"40")
    connected = False
    while time.time() < deadline and not connected:
        for m in _read_msgs(url):
            if m.startswith(b"2"):
                post(b"3")            # engine.io PING -> PONG
            elif m.startswith(b"40"):
                connected = True
    if not connected:
        raise RuntimeError("socket.io namespace did not connect in time")

    # 4) subscribe to the stop
    post(f'42["tabStart",[{stop_id},["*"]]]'.encode())

    # 5) read until we have the initial snapshot (at least one platform table)
    tables, seen = [], set()
    while time.time() < deadline:
        for m in _read_msgs(url):
            if m.startswith(b"2"):
                post(b"3")
                continue
            if m.startswith(b"42"):
                try:
                    name, payload = json.loads(m[2:])
                except Exception:
                    continue
                if name == "tabs":
                    for plat in payload:
                        if isinstance(plat, dict) and "tab" in plat:
                            k = plat.get("nastupiste")
                            if k not in seen:
                                seen.add(k)
                                tables.append(plat)
        if seen:
            time.sleep(0.5)           # let the rest of the snapshot land
            break
        time.sleep(0.2)

    if not tables:
        raise RuntimeError(f"no departure table returned for stop {stop_id}")
    return tables


def flatten(tables):
    """Flatten per-platform tables into one list of departures (+nastupiste)."""
    out = []
    for t in tables:
        for d in t.get("tab", []):
            d = dict(d)
            d["_nastupiste"] = t.get("nastupiste")
            out.append(d)
    return out


def _in_window(cas_ms, now_ms, horizon_min):
    delta = cas_ms - now_ms
    return 0 <= delta <= horizon_min * 60000


def select(deps, lines=None, horizon_min=60, dests=None):
    """Filter flattened departures by line set, destination keywords, window."""
    now_ms = int(time.time() * 1000)
    lines = set(str(x) for x in (lines or TARGETS))
    keep = []
    for d in deps:
        if str(d.get("linka")) not in lines:
            continue
        dest = (d.get("cielStr") or "") + " " + (d.get("konecnaZstr") or "")
        if dests and not any(k in dest for k in dests):
            continue
        if d.get("cas") and _in_window(d["cas"], now_ms, horizon_min):
            keep.append(d)
        elif d.get("casCP") and _in_window(d["casCP"], now_ms, horizon_min):
            keep.append(d)
    keep.sort(key=lambda d: d.get("cas") or d.get("casCP") or 0)
    return keep


def _fmt(ms):
    return datetime.datetime.fromtimestamp(ms / 1000).strftime("%H:%M")


def main():
    ap = argparse.ArgumentParser(description="imhd.sk live next departures")
    ap.add_argument("--stop", type=int, default=IMHD_STOP_ID)
    ap.add_argument("--lines", default=",".join(TARGETS), help="comma list, e.g. 32,49")
    ap.add_argument("--horizon", type=int, default=60, help="minutes to look ahead")
    ap.add_argument("--all-lines", action="store_true", help="ignore the target-line filter")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    tables = fetch_departures(a.stop)
    deps = flatten(tables)

    if a.all_lines:
        keep = select(deps, lines={d["linka"] for d in deps}, horizon_min=a.horizon)
    else:
        lines = [x for x in a.lines.split(",") if x]
        dests = [TARGETS[x]["imhd_dest"] for x in lines if x in TARGETS]
        keep = select(deps, lines=lines, horizon_min=a.horizon, dests=dests)

    if a.json:
        slim = [{
            "line": d.get("linka"), "time": _fmt(d.get("cas") or d.get("casCP") or 0),
            "min": int(((d.get("cas") or d.get("casCP")) - time.time() * 1000) / 60000),
            "type": d.get("typ"), "dest": d.get("cielStr"), "platform": d.get("_nastupiste"),
        } for d in keep]
        print(json.dumps({"stop": a.stop, "now": _fmt(int(time.time() * 1000)), "departures": slim}, ensure_ascii=False, indent=2))
        return

    now = _fmt(int(time.time() * 1000))
    print(f"Karpatská (stop {a.stop})  now={now}  next {a.horizon} min")
    print("-" * 62)
    for d in keep:
        ms = d.get("cas") or d.get("casCP")
        mins = int((ms - time.time() * 1000) / 60000)
        tag = "live" if d.get("typ") == "online" else "sched"
        delta = d.get("casDelta")
        dstr = f"  ({delta:+d}s)" if isinstance(delta, int) and delta else ""
        print(f"  {_fmt(ms)}  +{mins:02d}m  [{str(d.get('linka')):>4}]  {tag:5}  {dstr:8}  -> {d.get('cielStr')}  (nas {d.get('_nastupiste')})")
    if not keep:
        print("  (none in window)")


if __name__ == "__main__":
    main()
