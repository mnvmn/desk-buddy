#!/usr/bin/env python3
"""Next-hour overview for the travel panel: scheduled (GTFS) + live (imhd).

This is the single "show me the next hour" command. It prints:

  1. NEXT HOUR  — the full scheduled timetable (IDS BK GTFS) for the two
     target lines at Karpatská, interleaved by time. Deterministic, covers a
     complete 60 minutes (the imhd live feed only reaches ~45 min out).
  2. LIVE       — what the imhd real-time feed currently reports (vehicles
     actually approaching), with live-vs-schedule deltas. This is the source
     the panel will use to correct the near-term times.

Usage:
  python next_hour.py                  # now .. +60 min
  python next_hour.py --at 14:50       # from a given time today
  python next_hour.py --horizon 90 --no-live
"""
import argparse
import datetime as dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gtfs_next_hour as gnh
import imhd_realtime as imhd
from travel_config import TARGETS


def main():
    ap = argparse.ArgumentParser(description="next-hour overview (GTFS + live)")
    ap.add_argument("--at", help="start time HH:MM (default now)")
    ap.add_argument("--day", help="date YYYY-MM-DD (default today)")
    ap.add_argument("--horizon", type=int, default=60)
    ap.add_argument("--no-live", action="store_true", help="skip the imhd live section")
    a = ap.parse_args()

    now = dt.datetime.now()
    if a.at:
        when = dt.datetime.combine(now.date(), dt.datetime.strptime(a.at, "%H:%M").time())
    else:
        when = now

    # ---- 1) scheduled next hour (GTFS) -------------------------------------
    path = gnh.ensure_gtfs(max_age_h=24)
    g = gnh.load_gtfs(path)
    res = gnh.next_hour(g, when=when, horizon_min=a.horizon)
    res.sort(key=lambda r: r["sec"])

    line_names = {l: t["label"] for l, t in TARGETS.items()}
    print(f"Karpatská — next {a.horizon} min  (from {when.strftime('%H:%M')}, {now.date()})")
    print("  lines: " + "  |  ".join(line_names[l] for l in TARGETS))
    print("-" * 66)
    print(" NEXT HOUR (scheduled)")
    for r in res:
        print(f"   +{r['minutes']:02d}m  {r['hhmm']}   L{r['line']}   {line_names[r['line']].split(' ',1)[1]}")
    if not res:
        print("   (none in window)")

    # ---- 2) live real-time cross-check ------------------------------------
    if not a.no_live:
        print("-" * 66)
        try:
            tables = imhd.fetch_departures()
            deps = imhd.flatten(tables)
            dests = [t["imhd_dest"] for t in TARGETS.values()]
            live = imhd.select(deps, lines=set(TARGETS), horizon_min=min(a.horizon, 45), dests=dests)
            now_s = dt.datetime.now().strftime("%H:%M")
            print(f" LIVE (real-time feed, as of {now_s})")
            for d in live:
                ms = d.get("cas") or d.get("casCP")
                mins = int((ms - dt.datetime.now().timestamp() * 1000) / 60000)
                delta = d.get("casDelta")
                dstr = f"{delta:+d}s" if isinstance(delta, int) and delta else "   "
                tag = "live" if d.get("typ") == "online" else "cp"
                print(f"   +{mins:02d}m  {dt.datetime.fromtimestamp(ms/1000).strftime('%H:%M')}   "
                      f"L{d.get('linka')}   {tag:4}  {dstr:>5}  -> {d.get('cielStr')}")
            if not live:
                print("   (none in window)")
        except Exception as e:
            print(f"   (live feed unavailable: {e})")


if __name__ == "__main__":
    main()
