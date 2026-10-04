"""DeskBuddy parity test — capture the widget AND the ESP32 device, compare them.

Both frontends render the same `BuddyState` from the same llama.cpp server,
so given the same live server they should agree on phase, model, and (while
generating) a sane tok/s. This script:

  1. captures the widget in each deterministic scene mode
     (`DESKBUDDY_SCENE` + `EFRAME_SCREENSHOT_TO` — see widget/tests/screenshot_test.rs)
     -> widget_*.png, plus a golden-match check;
  2. drives the ESP32 device through its states over USB-CDC:
       - `idle`: two consecutive `poll: idle` lines, then `CAP`;
       - `generating`: fires a long generation at llama-server, waits for the
         device's own `poll: WORKING`, then sends `CAP` for N frames spaced
         one poll apart (~5.2 s) so the device numbers advance each frame;
  3. composites a contact sheet (widget goldens | device frames) and writes a
     machine-readable report with per-frame validation.

Usage:
  python tests/parity/parity_test.py [--port COM3] [--frames 3]
                                     [--widget-only] [--device-only]
                                     [--skip-idle] [--build]

Prereqs: widget binary built (`--build` does it), pyserial + Pillow,
device plugged in on --port, llama-server up on m.tower1:11444.
Outputs go to tests/parity/runs/<UTC-stamp>/ so runs never clobber each other.
"""
import argparse
import base64
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.request
from datetime import datetime, timezone

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
WIDGET_DIR = os.path.join(ROOT, "widget")
WIDGET_BIN = os.path.join(WIDGET_DIR, "target", "debug", "deskbuddy_widget.exe")
GOLDEN_DIR = os.path.join(WIDGET_DIR, "tests", "snapshots")
SCENES = ["generating", "idle", "down", "prompting"]
SERVER = "http://m.tower1:11444"
PANEL_W = PANEL_H = 480

TILE_RE = re.compile(rb"^TILE (\d+) (\d+) (\d+) (\d+)$")
# Firmware poll line, 2-server format (parity-fixes):
#   poll: srv1 <phase> <rate> tok/s ctx=<N>/<N> rem=<M> tot=<T> | srv2 <...same...>
# phase is WORKING | PROMPTING | idle. `rem` is the busy slot's n_remain
# (int32, -1 when no bounded generation is running — the panel shows it only
# while generating). `tot` is the session token total from /metrics.
POLL_RE = re.compile(
    rb"poll: srv1 (WORKING|PROMPTING|idle) ([\d.]+) tok/s ctx=(\d+)/(\d+) "
    rb"rem=(-?\d+) tot=(\d+) \| srv2 (WORKING|PROMPTING|idle) ([\d.]+) tok/s "
    rb"ctx=(\d+)/(\d+) rem=(-?\d+) tot=(\d+)")

# The same long generation capture_live.py proved works (~120 tok/s -> ~30 s).
GEN_PROMPT = (
    "Write a 3000 word essay with at least 10 numbered sections, each several "
    "paragraphs long, on the complete history of lighthouse technology from "
    "ancient fires to modern automated LED beacons. Be thorough and do not "
    "stop early.")


def log(msg):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


# --------------------------------------------------------------------------
# Widget side
# --------------------------------------------------------------------------

def build_widget():
    if shutil.which("cargo") is None:
        # rustup shim location on this machine
        shim = os.path.expandvars(r"%USERPROFILE%\.cargo\bin")
        if os.path.isdir(shim):
            os.environ["PATH"] = shim + os.pathsep + os.environ["PATH"]
        else:
            sys.exit("cargo not found — run with --build after installing Rust")
    log("building widget (cargo build in widget/) ...")
    subprocess.run(["cargo", "build"], cwd=WIDGET_DIR, check=True)
    if not os.path.exists(WIDGET_BIN):
        sys.exit(f"widget binary not found after build: {WIDGET_BIN}")


def widget_capture(scene, out_png, live_server=None):
    """One deterministic render of the widget in `scene` (data mode).

    With `live_server`, instead of the frozen scene data the widget seeds its
    state from that LIVE server (`DESKBUDDY_LIVE_SERVER`) — the same source
    the device polls — so a capture taken next to a device frame reflects the
    same moment (parity P4: both frontends read the same server).
    """
    if os.path.exists(out_png):
        os.remove(out_png)
    env = dict(os.environ)
    if live_server:
        env["DESKBUDDY_LIVE_SERVER"] = live_server.replace("http://", "")
        env.pop("DESKBUDDY_SCENE", None)
    else:
        env["DESKBUDDY_SCENE"] = scene
        env.pop("DESKBUDDY_LIVE_SERVER", None)
    env["EFRAME_SCREENSHOT_TO"] = out_png
    # The screenshot hook is armed purely by the exported env var; make sure
    # we're not inheriting a stale one from the shell.
    env.pop("DESKBUDDY_MODE", None)
    p = subprocess.run(
        [WIDGET_BIN], env=env, capture_output=True, timeout=120)
    if not os.path.exists(out_png):
        sys.exit(f"widget capture failed for {scene!r}: "
                 f"exit={p.returncode} stderr={p.stderr.decode(errors='replace')[:400]}")
    return out_png


def golden_stats(scene):
    """How close is the fresh capture to the committed golden? (mean diff)."""
    from PIL import ImageChops
    golden = os.path.join(GOLDEN_DIR, f"{scene}.png")
    fresh = os.path.join(GOLDEN_DIR, f"_parity_{scene}.png")
    if not (os.path.exists(golden) and os.path.exists(fresh)):
        return None
    a, b = Image.open(golden).convert("RGB"), Image.open(fresh).convert("RGB")
    if a.size != b.size:
        return {"match": False, "reason": f"size {a.size} vs {b.size}"}
    diff = ImageChops.difference(a, b)
    total = px = 0
    for ch in "RGB":
        h = diff.getchannel(ch).histogram()  # 256 bins: bin index == value
        px += sum(h)
        total += sum(i * c for i, c in enumerate(h))
    mean = total / max(px, 1)
    return {"match": mean < 1.5, "mean_diff": round(mean, 3)}


# --------------------------------------------------------------------------
# Device side
# --------------------------------------------------------------------------

class Panel:
    """Composites one TILE...END stream into a 480x480 RGB image."""

    def __init__(self):
        self.img = Image.new("RGB", (PANEL_W, PANEL_H), (0, 0, 0))
        self.tiles = 0
        self._tile = None

    def feed(self, line):
        """Returns 'tile' | 'end' | None. `line` is bytes, no newline."""
        m = TILE_RE.match(line)
        if m:
            x, y, w, h = (int(v) for v in m.groups())
            self._tile = [x, y, w, h, w * h * 2, bytearray()]
            return "tile"
        if line == b"END":
            return "end"
        t = self._tile
        if t is not None and line:
            t[5] += base64.b64decode(line + b"=" * ((-len(line)) % 4))
            if len(t[5]) >= t[4]:
                raw = bytes(t[5])[:t[4]]
                px = struct.unpack(f"<{t[2]*t[3]}H", raw)
                rgb = bytearray()
                for p in px:
                    rgb += bytes((((p >> 11) & 31) * 255 // 31,
                                  ((p >> 5) & 63) * 255 // 63,
                                  (p & 31) * 255 // 31))
                self.img.paste(Image.frombytes("RGB", (t[2], t[3]), bytes(rgb)),
                               (t[0], t[1]))
                self.tiles += 1
                self._tile = None
        return None


class SerialReader:
    """Line-oriented reader for the device CDC console."""

    def __init__(self, port):
        import serial
        self.s = serial.Serial(port, 115200, timeout=0.05)
        self.s.reset_input_buffer()
        self.buf = b""

    def readline(self, deadline):
        while time.time() < deadline:
            d = self.s.read(max(self.s.in_waiting, 1))
            if d:
                self.buf += d
                if b"\n" in self.buf:
                    line, self.buf = self.buf.split(b"\n", 1)
                    return line.strip()
            time.sleep(0.01)
        return None

    def send_cap(self):
        self.s.write(b"CAP\n")
        self.s.flush()

    def close(self):
        self.s.close()


def read_stream(r: SerialReader, panel: Panel, deadline):
    """Consume one TILE...END stream. Returns (tiles, latest_poll_line):
    the most recent `poll:` line seen while draining the console (usually the
    one printed in the iteration that just rendered the state we captured —
    see the one-poll-lag note in wiki/parity/README.md)."""
    latest_poll = None
    while time.time() < deadline:
        line = r.readline(deadline)
        if line is None:
            break
        m = POLL_RE.match(line)
        if m:
            latest_poll = line.decode().strip()
            continue
        if panel.feed(line) == "end":
            break
    return panel.tiles, latest_poll


def gen_request(server, max_tokens):
    """Fire one long generation; returns the thread (polls while it streams)."""
    req = urllib.request.Request(
        server + "/v1/chat/completions",
        data=json.dumps({
            "model": "default", "max_tokens": max_tokens, "stream": True,
            "messages": [{"role": "user", "content": GEN_PROMPT}]}).encode(),
        headers={"Content-Type": "application/json"})

    def run():
        try:
            with urllib.request.urlopen(req, timeout=600) as resp:
                resp.read()
        except Exception as e:
            log(f"[gen] {e!r}")
    t = threading.Thread(target=run, daemon=True)
    t.start()
    return t


def server_up(server):
    try:
        with urllib.request.urlopen(server + "/slots", timeout=5) as r:
            return r.status == 200
    except Exception:
        return False


def capture_device_frames(port, frames, run_dir, skip_idle, server, max_tokens,
                          first_gen_hook=None):
    """Drive the device; returns (frames, notes). Each frame:
    {id, phase, at, png, poll, rate, remain}.

    State machine:
      IDLE    -> capture one idle frame (after 2 consecutive idle polls with
                 no generation running)
      GEN     -> fire a generation, capture one frame per poll while the
                 device reports WORKING, stop after `frames`

    `first_gen_hook` (callable, called once after the first generating frame
    lands) lets the caller grab a LIVE widget capture at the same moment the
    device is mid-generation (parity P4: both frontends read the same server
    while it's working). It runs in a worker thread so the device loop keeps
    polling; any exception is logged, never fatal.
    """
    import serial  # noqa: F401  (fail fast with a clear message if missing)
    r = SerialReader(port)
    out = []
    notes = []
    gen = None
    state = None
    last_poll = None
    last_poll_at = 0.0
    last_cap_at = 0.0
    idle_polls = 0
    got_idle = skip_idle
    gen_frames = 0
    t0 = time.time()
    hard_deadline = t0 + 420   # a pre-existing generation may still be running

    def do_cap(phase):
        nonlocal last_cap_at, gen_frames, last_poll
        r.send_cap()
        last_cap_at = time.time()
        p = Panel()
        n, stream_poll = read_stream(r, p, last_cap_at + 45)
        # One more short read: the poll line printed in the iteration that
        # just rendered our state can land just after the END of the stream.
        tail = r.readline(last_cap_at + 2.2)
        m_tail = POLL_RE.match(tail) if tail else None
        if m_tail:
            stream_poll = tail.decode().strip()
        if stream_poll:
            last_poll = stream_poll
        if n == 0:
            notes.append(f"{phase}: no tiles received")
            return None
        m2 = POLL_RE.match((last_poll or "").encode())
        # srv1 (the generation target) fields: rate=2, ctx_used=3, rem=5.
        rate = float(m2.group(2)) if m2 else 0.0
        used = int(m2.group(3)) if m2 else None
        remain = int(m2.group(5)) if m2 else None
        f = _frame(phase, p, last_poll, rate, remain, run_dir, used)
        out.append(f)
        if phase == "generating":
            gen_frames += 1
            if first_gen_hook is not None and gen_frames == 1:
                log("hook: live widget capture running (widget reads the same server)")
                t = threading.Thread(target=first_gen_hook, daemon=True)
                t.start()
        log(f"{phase} frame ok ({n} tiles)  {last_poll}")
        return f

    while time.time() < hard_deadline:
        line = r.readline(time.time() + 0.15)
        if line is not None:
            m = POLL_RE.match(line)
            if m:
                working = m.group(1) == b"WORKING"
                state = "WORKING" if working else "idle"
                last_poll = line.decode().strip()
                last_poll_at = time.time()
                idle_polls = idle_polls + 1 if (
                    state == "idle" and (gen is None or not gen.is_alive())) else 0
            else:
                # stray boot-capture tiles / boot log: drain silently
                continue

        now = time.time()
        gen_alive = gen is not None and gen.is_alive()

        # IDLE frame: two idle polls after the generation has finished
        if (not got_idle and not gen_alive and idle_polls >= 2
                and now - last_poll_at > 0.5):
            if do_cap("idle"):
                got_idle = True

        # Start a generation if none is running
        if not gen_alive and gen is None and not got_idle:
            log(f"firing generation at {server} (max_tokens={max_tokens})")
            gen = gen_request(server, max_tokens)

        # GENERATING frames: the device polls every 500 ms while working
        # (spec §3.3, parity fix P4), so space frames ~1.1 s apart: the
        # numbers visibly advance between frames while the run (~30 s for
        # 3000 tokens at ~120 tok/s) still yields plenty of frames.
        if (state == "WORKING" and not got_idle and gen_frames < frames
                and now - last_poll_at > 0.5 and now - last_cap_at > 1.1):
            do_cap("generating")

        if got_idle and gen_frames >= frames:
            break
    r.close()
    out.sort(key=lambda f: (f["phase"] != "generating", f["at"]))
    if not skip_idle and not got_idle:
        notes.append("idle frame was not captured")
    return out, notes


def _frame(phase, panel, poll, rate, remain, run_dir, used=None):
    fn = f"device_{phase}_{len(panel.img.size)}x{int(time.time()*1000) % 100000}.png"
    path = os.path.join(run_dir, "device", fn)
    panel.img.save(path)
    return {
        "id": fn, "phase": phase, "at": time.time(),
        "png": os.path.relpath(path, ROOT), "poll": poll,
        "rate": rate, "remain": remain, "used": used,
    }


# --------------------------------------------------------------------------
# Report + contact sheet
# --------------------------------------------------------------------------

def validate_frames(frames):
    notes = []
    gens = [f for f in frames if f["phase"] == "generating"]
    for i, f in enumerate(gens):
        if not (f["rate"] > 0):
            # Frames 1-2 can legitimately be 0.0: the device is still in
            # prompt processing (amber PROMPTING on the panel) with no
            # decoded tokens to compute a rate from.
            if i < 2:
                continue
            notes.append(f"{f['id']}: rate not positive ({f['rate']})")
        elif i >= 2 and not (20.0 <= f["rate"] <= 500.0):
            # A settled rate should be plausible for this box (~120 tok/s
            # measured) — a far-off value means the rate calc is wrong again.
            notes.append(f"{f['id']}: rate out of plausibility band ({f['rate']})")
        if f["remain"] is not None and f["remain"] <= 0:
            notes.append(f"{f['id']}: remain not positive ({f['remain']})")
    for a, b in zip(gens, gens[1:]):
        if a["remain"] is not None and b["remain"] is not None and \
                b["remain"] > a["remain"]:
            notes.append(f"{b['id']}: remain increased {a['remain']} -> {b['remain']}")
    if len(gens) < 2:
        notes.append("fewer than 2 generating frames captured")
    return notes


def make_sheet(run_dir, widget_shots, frames, goldens, live_widget=None):
    """Contact sheet: left = widget scenes, right = device frames.

    One label row per panel, capped to the cell width so captions never run
    into the neighboring column. `live_widget` is an optional extra widget
    panel (a LIVE capture seeded from the same server, taken while the device
    was mid-generation — parity P4) placed after the scene goldens."""
    H = 300
    PAD = 14
    LBL = 22
    panels = []
    for scene in SCENES:
        g = goldens.get(scene)
        path = widget_shots.get(scene) or (g or "")
        if os.path.exists(path):
            im = Image.open(path).convert("RGB")
            im = im.resize((int(im.width * H / im.height), H), Image.LANCZOS)
            label = f"widget {scene}"
        else:
            im = Image.new("RGB", (H, H), (24, 24, 28))
            label = f"widget {scene} (missing)"
        panels.append((label, im))
    if live_widget and os.path.exists(live_widget):
        im = Image.open(live_widget).convert("RGB")
        im = im.resize((int(im.width * H / im.height), H), Image.LANCZOS)
        panels.append(("widget LIVE (same server, mid-gen)", im))
    for f in frames:
        if os.path.exists(os.path.join(ROOT, f["png"])):
            im = Image.open(os.path.join(ROOT, f["png"])).convert("RGB")
            im = im.resize((int(im.width * H / im.height), H), Image.LANCZOS)
            # Keep the label to one cell: phase + rate + remain only.
            tail = ""
            if f["phase"] == "generating":
                tail = f"  {f['rate']:.0f} tok/s  {f['remain']} tok left"
            label = f"device {f['phase']}{tail}"
        else:
            im = Image.new("RGB", (H, H), (24, 24, 28))
            label = f"device {f['phase']} (missing)"
        panels.append((label, im))

    cols = 2
    rows = (len(panels) + cols - 1) // cols
    cell_w = max(im.width for _, im in panels)
    W = cols * (cell_w + PAD) + PAD
    Ht = rows * (H + LBL + PAD) + PAD + 40
    sheet = Image.new("RGB", (W, Ht), (16, 16, 20))
    d = ImageDraw.Draw(sheet)
    title = datetime.now(timezone.utc).strftime(
        "DeskBuddy parity  %Y-%m-%d %H:%M UTC")
    d.text((PAD, 10), title, fill=(240, 240, 245))

    # Truncate a label to the cell width so it never bleeds into the
    # neighbor. Measure with the (default) bitmap font via textlength.
    def fit(text, maxw):
        if d.textlength(text) <= maxw:
            return text
        while text and d.textlength(text + "…") > maxw:
            text = text[:-1]
        return text + "…"

    for i, (label, im) in enumerate(panels):
        r, c = divmod(i, cols)
        x = PAD + c * (cell_w + PAD)
        y = PAD + 40 + r * (H + LBL + PAD)
        d.text((x, y), fit(label, cell_w), fill=(154, 160, 170))
        sheet.paste(im, (x, y + LBL))
    out = os.path.join(run_dir, "sheet.png")
    sheet.save(out)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--frames", type=int, default=3,
                    help="generating frames to capture from the device")
    ap.add_argument("--max-tokens", type=int, default=3000)
    ap.add_argument("--widget-only", action="store_true")
    ap.add_argument("--device-only", action="store_true")
    ap.add_argument("--skip-idle", action="store_true",
                    help="don't wait for/capture the idle frame")
    ap.add_argument("--build", action="store_true",
                    help="cargo build the widget first")
    args = ap.parse_args()

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir = os.path.join(HERE, "runs", stamp)
    os.makedirs(os.path.join(run_dir, "widget"), exist_ok=True)
    os.makedirs(os.path.join(run_dir, "device"), exist_ok=True)
    log(f"run dir: {run_dir}")

    report = {"run": stamp, "server": SERVER, "port": args.port,
              "frames_requested": args.frames}

    # ---- widget ----
    widget_shots, goldens = {}, {}
    if not args.device_only:
        if args.build or not os.path.exists(WIDGET_BIN):
            build_widget()
        for scene in SCENES:
            dst = os.path.join(run_dir, "widget", f"widget_{scene}.png")
            log(f"widget capture: {scene}")
            widget_capture(scene, dst)
            widget_shots[scene] = os.path.relpath(dst, ROOT)
        # golden check: copy fresh captures into the snapshot dir, diff
        for scene in SCENES:
            dst = os.path.join(GOLDEN_DIR, f"_parity_{scene}.png")
            shutil.copyfile(os.path.join(run_dir, "widget", f"widget_{scene}.png"),
                            dst)
            goldens[scene] = golden_stats(scene)
        report["widget"] = {
            "shots": widget_shots,
            "goldens": goldens,
            "ok": all(g and g["match"] for g in goldens.values())
            if goldens else False,
        }

    # ---- device ----
    frames, device_notes = [], []
    live_widget_png = None
    if not args.widget_only:
        if not server_up(SERVER):
            log(f"WARNING: llama-server not reachable at {SERVER} — "
                "device will likely show down; continuing")
        if not args.device_only:
            # P4: once the device has its first mid-generation frame, capture
            # the WIDGET live from the same server so both frontends are read
            # at (roughly) the same moment. Runs in its own thread.
            live_path = os.path.join(run_dir, "widget", "widget_live.png")
            live_done = threading.Event()
            live_err = []

            def first_gen_hook():
                try:
                    widget_capture("generating", live_path, live_server=SERVER)
                except BaseException as e:  # noqa: BLE001 — never kill the loop
                    live_err.append(repr(e))
                    log(f"live widget capture failed: {e!r}")
                finally:
                    live_done.set()

            frames, device_notes = capture_device_frames(
                args.port, args.frames, run_dir, args.skip_idle,
                SERVER, args.max_tokens, first_gen_hook=first_gen_hook)
            if live_err:
                device_notes.append(f"live widget capture failed: {live_err[0]}")
            log("waiting for live widget capture to finish ...")
            live_done.wait(120)
            if os.path.exists(live_path):
                live_widget_png = live_path
                report.setdefault("widget", {})["live"] = os.path.relpath(
                    live_widget_png, ROOT)
        else:
            frames, device_notes = capture_device_frames(
                args.port, args.frames, run_dir, args.skip_idle,
                SERVER, args.max_tokens)
    frames = sorted(frames, key=lambda f: (f["phase"] != "generating", f["at"]))
    if not args.widget_only:
        report["device"] = {"frames": frames, "notes": device_notes,
                            "validation": validate_frames(frames)}
    else:
        report["device"] = None

    with open(os.path.join(run_dir, "report.json"), "w") as f:
        json.dump(report, f, indent=2)

    if widget_shots or frames:
        sheet = make_sheet(run_dir, widget_shots, frames,
                           {s: os.path.join(GOLDEN_DIR, f"{s}.png")
                            for s in SCENES},
                           live_widget=live_widget_png)
        report["sheet"] = os.path.relpath(sheet, ROOT)

    # ---- summary ----
    print("\n=== parity summary ===")
    if "widget" in report:
        print(f"widget: {'OK' if report['widget']['ok'] else 'MISMATCH vs goldens'}")
        for s, g in goldens.items():
            if g:
                print(f"  {s}: {'match' if g['match'] else 'MISMATCH'} "
                      f"(mean {g.get('mean_diff', g.get('reason'))})")
    if report.get("device"):
        for fr in frames:
            print(f"device {fr['phase']}: {fr['poll']}")
        v = report["device"]["validation"]
        print(f"device: {'OK' if not v else v}")
        for n in device_notes:
            print(f"note: {n}")
    print(f"report: {os.path.join(run_dir, 'report.json')}")
    if frames or widget_shots:
        print(f"sheet:  {os.path.join(run_dir, 'sheet.png')}")


if __name__ == "__main__":
    main()
