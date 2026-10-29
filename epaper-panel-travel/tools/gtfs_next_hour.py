#!/usr/bin/env python3
"""Compute the SCHEDULED next-hour departures from the IDS BK GTFS feed.

The real-time imhd feed only covers the next ~30-45 min (vehicles that are
actively approaching). For a clean "next 60 minutes" list we use the static
GTFS timetable (IDS BK open data), which has the full day. This tool:

  1. downloads + caches the GTFS zip (data/idsbk_gtfs.zip) if missing,
  2. picks the service(s) active for a given date (calendar + calendar_dates),
  3. lists every Karpatská departure for the target lines,
  4. prints the ones inside the requested horizon from ``now`` (or --at).

Verified against the live imhd feed and the imhd "cestovný poriadok":
both target lines run on a 15-minute pattern that GTFS reproduces exactly.

Usage:
  python gtfs_next_hour.py                 # now .. +60 min, both target lines
  python gtfs_next_hour.py --at 14:50      # from a specific time today
  python gtfs_next_hour.py --day 2026-10-19 --at 07:30
  python gtfs_next_hour.py --horizon 90 --all
"""
import argparse
import csv
import datetime as dt
import io
import os
import sys
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from travel_config import (GTFS_URL, GTFS_ZIP, GTFS_STOP_CODE, TARGETS, USER_AGENT)


# --------------------------------------------------------------- GTFS I/O
def ensure_gtfs(path=GTFS_ZIP, url=GTFS_URL, max_age_h=None):
    """Download the GTFS zip to ``path`` if missing (or stale). Returns path."""
    if max_age_h is not None:
        if path.exists() and (dt.datetime.now().timestamp() - path.stat().st_mtime) < max_age_h * 3600:
            return path
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=180) as r, open(path, "wb") as f:
            f.write(r.read())
    return path


def _txt(z, name):
    for n in z.namelist():
        if os.path.basename(n) == name:
            return io.TextIOWrapper(z.open(n), encoding="utf-8-sig")
    return None


def load_gtfs(path):
    """Load the tables we need into memory."""
    z = zipfile.ZipFile(path)
    d = {}
    for name in ("routes.txt", "stops.txt", "trips.txt", "stop_times.txt",
                 "calendar.txt", "calendar_dates.txt"):
        t = _txt(z, name)
        if t is None:
            continue
        with t:
            d[name] = list(csv.DictReader(t))
    z.close()
    return d


# --------------------------------------------------------------- helpers
def _t2sec(s):
    if not s:
        return None
    parts = s.strip().split(":")
    if len(parts) < 2:
        return None
    h, m = int(parts[0]), int(parts[1])
    sec = int(parts[2]) if len(parts) > 2 else 0
    return h * 3600 + m * 60 + sec


def _sec2t(s):
    return f"{s // 3600:02d}:{(s % 3600) // 60:02d}"


def active_services(g, date):
    """Set of service_ids running on ``date`` (calendar + calendar_dates)."""
    day = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"][date.weekday()]
    active = {r["service_id"] for r in g["calendar.txt"] if r.get(day) == "1"}
    for r in g["calendar_dates.txt"]:
        d = r.get("date", "")[:10]
        if d == date.strftime("%Y%m%d"):
            if r.get("exception_type") == "1":
                active.add(r["service_id"])
            elif r.get("exception_type") == "2":
                active.discard(r["service_id"])
    return active


# --------------------------------------------------------------- core
def next_hour(g, stop_code=GTFS_STOP_CODE, targets=TARGETS,
              when=None, horizon_min=60, date=None):
    """Return [{sec, hhmm, minutes, line, headsign, sub_stop}] for departures
    from ``stop_code`` inside [when, when+horizon].

    ``targets`` is a {line: spec} dict (see travel_config.TARGETS) or ``None``
    to return every line that serves the stop.
    """
    date = date or (when or dt.datetime.now()).date()
    when = when or dt.datetime.now()
    now_sec = when.hour * 3600 + when.minute * 60 + when.second
    svc = active_services(g, date)

    karp_ids = {r["stop_id"] for r in g["stops.txt"] if r.get("stop_code") == stop_code}
    route_short = {r["route_id"]: r.get("route_short_name") for r in g["routes.txt"]}
    trip_by_id = {r["trip_id"]: r for r in g["trips.txt"]}

    rows = []
    for st in g["stop_times.txt"]:
        if st["stop_id"] not in karp_ids:
            continue
        trip = trip_by_id.get(st["trip_id"])
        if not trip:
            continue
        line = route_short.get(trip["route_id"])
        if line is None:
            continue
        spec = targets.get(line) if targets is not None else None
        if targets is not None and spec is None:
            continue
        if trip.get("service_id") not in svc:
            continue
        head = trip.get("trip_headsign", "")
        sub = st["stop_id"][-1]
        if spec is not None:
            if spec.get("direction_id") and trip.get("direction_id", "0") != spec["direction_id"]:
                continue
            if spec.get("sub_stop") and sub != spec["sub_stop"]:
                continue
            hs = spec.get("gtfs_headsign")
            if hs and not any(k in head for k in hs):
                continue
        sec = _t2sec(st.get("departure_time") or st.get("arrival_time"))
        if sec is None:
            continue
        rows.append({"sec": sec, "line": line, "headsign": head, "sub_stop": sub})

    rows.sort(key=lambda r: r["sec"])
    out = []
    for r in rows:
        delta = r["sec"] - now_sec
        if 0 <= delta <= horizon_min * 60:
            out.append({**r, "hhmm": _sec2t(r["sec"]), "minutes": int(delta // 60)})
    return out


def main():
    ap = argparse.ArgumentParser(description="GTFS scheduled next-hour departures")
    ap.add_argument("--at", help="start time HH:MM (default: now)")
    ap.add_argument("--day", help="date YYYY-MM-DD (default: today)")
    ap.add_argument("--horizon", type=int, default=60)
    ap.add_argument("--all", action="store_true", help="ignore target-line filter")
    ap.add_argument("--refresh", action="store_true", help="re-download GTFS even if cached")
    a = ap.parse_args()

    now = dt.datetime.now()
    if a.day:
        date = dt.date.fromisoformat(a.day)
        at_time = dt.datetime.strptime(a.at, "%H:%M").time() if a.at else now.time()
        when = dt.datetime.combine(date, at_time)
    else:
        date = now.date()
        when = (dt.datetime.combine(now.date(), dt.datetime.strptime(a.at, "%H:%M").time())
                if a.at else now)

    path = ensure_gtfs(max_age_h=0 if a.refresh else 24)
    g = load_gtfs(path)

    if a.all:
        targets = None          # every line that serves the stop
    else:
        targets = TARGETS

    res = next_hour(g, when=when, horizon_min=a.horizon, date=date, targets=targets)

    print(f"Karpatská  from {when.strftime('%Y-%m-%d %H:%M')}  next {a.horizon} min  "
          f"({len(res)} departures)")
    print("-" * 58)
    for r in res:
        print(f"  +{r['minutes']:02d}m  {r['hhmm']}  L{r['line']}  substop {r['sub_stop']}  {r['headsign']}")
    if not res:
        print("  (none in window)")


if __name__ == "__main__":
    main()
