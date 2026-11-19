#!/usr/bin/env python3
"""A short WAV of every program, from the host simulators, and numbers for each.

    tools/sounds.py [--out DIR] [--jobs N] [ID ...]

Builds the simulators and renders one clip per program, each a fixed scripted performance with the rev E
controls (the left knob, a hand over the sensor, REC):

  samplooler  tools/sounds/samplooler.c: REC over a sung phrase, the loop tipped faster and backwards, rolled for
              the delay and the stutter, pushed with the left knob, the hand closing the filter
  huff wreck mantra band pixel sing
              each instrument's own simulator (instruments/<id>/tools/sim/run.sh inst), the clip it writes for the
              preview page (<id>-rev-e.wav; PIXEL's is pixel-rev-e-write.wav)

Writes to DIR (default tools/sounds/out): <id>.wav (16-bit, 32 kHz, mono), sounds.json, index.html, and the
simulators' output in logs/. The simulators run with SIM_WAV_F32 set, so the numbers are taken from the float
samples as they came out, before anything is clamped.

Per clip: peak, RMS and DC; NaN or inf samples; samples at or past full scale (hard clipping); samples near the
output's ceiling (the instruments' limiter holds 0.891, the Samplooler's soft clip 0.9); clicks, here any jump of
more than 0.5 between two samples (a square wave or a drum hit makes them too: compare with the last run rather
than read it alone). Exit 1 if a clip has a NaN or inf, is silent (RMS under -60 dBFS), or clips hard (any sample at
or past full scale); 2 if a clip could not be made. Needs gcc and python3, nothing else.
"""
import argparse
import array
import concurrent.futures
import hashlib
import html
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 32000

# id, name, the simulator's clip, what happens in it
PROGRAMS = [
    ("samplooler", "Samplooler", None,
     "0:00 REC down over a sung phrase, let go at 2.6 s: it loops. 4.0 tipped further: faster. 5.2 nose up: backwards. "
     "7.0 rolled left: tape delay. 8.2 rolled right: stutter. 9.2 the left knob pushes the tape. 10.0 a hand comes "
     "down over the sensor and lifts: the filter closes and opens."),
    ("huff", "HUFF", "huff-rev-e.wav",
     "Blown. The left knob through the octaves, a hand down over the sensor and lifted (the mute), then waved (a wah)."),
    ("wreck", "WRECK", "wreck-rev-e.wav",
     "The left knob to crush, then to drive, then back; a hand down over the sensor and lifted (the roll)."),
    ("mantra", "MANTRA", "mantra-rev-e.wav",
     "The left knob moves the note; a hand over the sensor is a hand over his mouth."),
    ("band", "BAND", "band-rev-e.wav",
     "The left knob sets the live layer's level; a hand over the sensor plays that layer."),
    ("pixel", "PIXEL", "pixel-rev-e-write.wav",
     "A bar written with the two knobs (the left one moves, the right one writes), then REC plays it."),
    ("sing", "SING", "sing-rev-e.wav",
     "The left knob is the choir; a hand over the sensor throws the voice into the echo."),
]

SILENT_DB = -60.0       # RMS under this is silence
CEILING = 0.88          # near the output's ceiling (the limiter's 0.891, the soft clip's 0.9)
CLICK = 0.5             # a jump between two samples larger than this counts as a click


def read_f32_wav(path):
    """The float samples of a mono 32-bit float WAV (or 16-bit PCM, scaled to +-1)."""
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV")
    i, fmt, bits, data = 12, None, None, None
    while i + 8 <= len(b):
        cid, size = b[i:i + 4], struct.unpack_from("<I", b, i + 4)[0]
        if cid == b"fmt ":
            fmt, ch, sr = struct.unpack_from("<HHI", b, i + 8)
            bits = struct.unpack_from("<H", b, i + 22)[0]
            if ch != 1 or sr != SR:
                raise ValueError(f"{path}: {ch} channels at {sr} Hz, expected mono at {SR}")
        elif cid == b"data":
            data = b[i + 8:i + 8 + size]
        i += 8 + size + (size & 1)
    if fmt == 3 and bits == 32:
        a = array.array("f")
        a.frombytes(data[:len(data) // 4 * 4])
        return list(a)
    if fmt == 1 and bits == 16:
        a = array.array("h")
        a.frombytes(data[:len(data) // 2 * 2])
        return [v / 32768.0 for v in a]
    raise ValueError(f"{path}: format {fmt}, {bits} bits")


def write_wav16(path, x):
    q = array.array("h")
    for v in x:
        if not math.isfinite(v):
            v = 0.0
        v = 1.0 if v > 1.0 else -1.0 if v < -1.0 else v
        q.append(int(v * 32767.0))
    if sys.byteorder != "little":
        q.byteswap()
    pcm = q.tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, SR, SR * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)


def db(v):
    return round(20.0 * math.log10(v), 1) if v > 0 else None


def measure(x):
    n, bad, over, ceil_n, clicks = len(x), 0, 0, 0, 0
    peak = s = s2 = worst = 0.0
    worst_at = 0
    last = 0.0
    for i, v in enumerate(x):
        if not math.isfinite(v):
            bad += 1
            continue
        a = abs(v)
        if a > peak:
            peak = a
        if a >= 1.0:
            over += 1
        if a >= CEILING:
            ceil_n += 1
        s += v
        s2 += v * v
        d = abs(v - last)
        if d > CLICK:
            clicks += 1
        if d > worst:
            worst, worst_at = d, i
        last = v
    good = max(n - bad, 1)
    rms = math.sqrt(s2 / good)
    m = {
        "seconds": round(n / SR, 3),
        "peak": round(peak, 4), "peak_dbfs": db(peak),
        "rms_dbfs": db(rms),
        "dc": round(s / good, 5),
        "nan_inf": bad,
        "over_full_scale": over,
        "near_ceiling_pct": round(100.0 * ceil_n / max(n, 1), 3),
        "clicks": clicks,
        "worst_jump": round(worst, 4), "worst_jump_at_s": round(worst_at / SR, 3),
        "silent": rms == 0.0 or 20.0 * math.log10(rms) < SILENT_DB,
    }
    why = []
    if bad:
        why.append(f"{bad} NaN or inf")
    if m["silent"]:
        why.append(f"silent (RMS {m['rms_dbfs']} dBFS)")
    if over:
        why.append(f"clips hard ({over} samples at or past full scale)")
    m["ok"] = not why
    m["problems"] = why
    return m


def run(cmd, cwd, log, env=None):
    with open(log, "w") as f:
        return subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env).returncode


def last_line(path):
    try:
        with open(path, errors="replace") as f:
            lines = [l.strip() for l in f if l.strip()]
        return lines[-1] if lines else ""
    except OSError:
        return ""


def make_samplooler(out, logs):
    """Build tools/sounds/samplooler.c against the firmware's files and render its clip."""
    m = os.path.join(ROOT, "firmware", "main")
    exe = os.path.join(out, "build", "samplooler")
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    log = os.path.join(logs, "samplooler.log")
    cc = ["gcc", "-std=gnu17", "-O2", "-ffast-math", "-fno-math-errno", "-Wall", "-Wextra", "-Werror", "-Wdouble-promotion",
          "-Wno-format-truncation", "-I" + os.path.join(ROOT, "tools", "host"), "-I" + m, "-o", exe,
          os.path.join(ROOT, "tools", "sounds", "samplooler.c")] + \
         [os.path.join(m, f) for f in ("recorder_f32.c", "fx.c", "gesture.c", "tune.c")] + ["-lm"]
    if run(cc, ROOT, log) != 0:
        return None, "the build failed"
    wav = os.path.join(out, "build", "samplooler.f32.wav")
    if run([exe, wav], ROOT, os.path.join(logs, "samplooler-run.log")) != 0 or not os.path.isfile(wav):
        return None, "the clip program failed"
    return wav, "the firmware's recorder, fx and gestures"


def make_instrument(pid, clip, logs):
    sim = os.path.join(ROOT, "instruments", pid, "tools", "sim")
    log = os.path.join(logs, f"{pid}.log")
    env = dict(os.environ, SIM_WAV_F32="1")
    run(["sh", "run.sh", "inst"], sim, log, env)     # its checks may fail (the wall-clock ones on a busy machine): the clip is what is wanted here
    wav = os.path.join(sim, "out", clip)
    if not os.path.isfile(wav):
        return None, "no clip: " + last_line(log)
    return wav, last_line(log)


def page(results, commit):
    rows = []
    for r in results:
        m = r.get("m")
        if not m:
            rows.append(f"<section class='bad'><h2>{html.escape(r['name'])}</h2><p>No clip: {html.escape(r['note'])}</p></section>")
            continue
        status = "ok" if m["ok"] else "FAIL: " + html.escape("; ".join(m["problems"]))
        nums = (f"peak {m['peak']} ({m['peak_dbfs']} dBFS) · RMS {m['rms_dbfs']} dBFS · DC {m['dc']} · NaN/inf {m['nan_inf']} · "
                f"past full scale {m['over_full_scale']} · near the ceiling {m['near_ceiling_pct']} % · "
                f"clicks {m['clicks']} (worst jump {m['worst_jump']} at {m['worst_jump_at_s']} s)")
        rows.append(f"<section class='{'ok' if m['ok'] else 'bad'}'><h2>{html.escape(r['name'])} <small>{m['seconds']} s · {status}</small></h2>"
                    f"<audio controls preload='none' src='{r['id']}.wav'></audio><p>{html.escape(r['what'])}</p><p class='n'>{nums}</p></section>")
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sounds {html.escape(commit)}</title>
<style>
body {{ font: 15px/1.45 system-ui, sans-serif; max-width: 760px; margin: 2em auto; padding: 0 16px; background: #fff; color: #111; }}
h1 {{ font-size: 1.3em; }} h2 {{ font-size: 1.05em; margin: 0 0 .4em; }} small {{ font-weight: normal; color: #555; }}
section {{ border-left: 4px solid #2a2; padding: .6em .9em; margin: 1em 0; background: #f6f6f6; }}
section.bad {{ border-color: #c22; }} audio {{ width: 100%; }} .n {{ font: 13px/1.4 ui-monospace, monospace; color: #333; }}
@media (prefers-color-scheme: dark) {{ body {{ background: #111; color: #eee; }} section {{ background: #1c1c1c; }} small, .n {{ color: #bbb; }} }}
</style></head><body>
<h1>Sounds of {html.escape(commit)}</h1>
<p>Each program in the host simulator, played the same way every time. Made by <code>tools/sounds.py</code>.</p>
{chr(10).join(rows)}
</body></html>
"""


def main():
    ap = argparse.ArgumentParser(description="A short WAV of every program, from the host simulators, and numbers for each.")
    ap.add_argument("--out", default=os.path.join(ROOT, "tools", "sounds", "out"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2, help="simulators run at once")
    ap.add_argument("ids", nargs="*", help="only these programs")
    a = ap.parse_args()
    progs = [p for p in PROGRAMS if not a.ids or p[0] in a.ids]
    out = os.path.abspath(a.out)
    logs = os.path.join(out, "logs")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(logs)
    t0 = time.monotonic()

    made, took = {}, {}
    def job(p):
        s = time.monotonic()
        r = make_samplooler(out, logs) if p[0] == "samplooler" else make_instrument(p[0], p[2], logs)
        took[p[0]] = time.monotonic() - s
        return p[0], r
    # the slowest first (BAND's simulator takes the longest), so the others fill in round it
    order = sorted(progs, key=lambda p: ["band", "huff", "pixel", "wreck", "mantra", "sing", "samplooler"].index(p[0]))
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        for pid, r in ex.map(job, order):
            made[pid] = r
            print(f"  {pid:<11} {took[pid]:6.1f} s  {r[1]}", flush=True)

    results, failed, missing = [], 0, 0
    for pid, name, _, what in progs:
        wav, note = made[pid]
        r = {"id": pid, "name": name, "what": what, "note": note}
        if wav is None:
            missing += 1
        else:
            x = read_f32_wav(wav)
            r["m"] = measure(x)
            dst = os.path.join(out, pid + ".wav")
            write_wav16(dst, x)
            with open(dst, "rb") as f:
                r["m"]["sha256"] = hashlib.sha256(f.read()).hexdigest()
            if not r["m"]["ok"]:
                failed += 1
        results.append(r)

    commit = os.environ.get("GITHUB_SHA", "")[:9]
    if not commit:
        try:
            commit = subprocess.run(["git", "rev-parse", "--short=9", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
        except OSError:
            pass
    commit = commit or "?"
    doc = {"commit": commit, "sample_rate": SR, "silent_below_dbfs": SILENT_DB, "ceiling": CEILING, "click_jump": CLICK,
           "clips": [{"id": r["id"], "name": r["name"], "file": r["id"] + ".wav" if "m" in r else None, **(r.get("m") or {"ok": False, "problems": [r["note"]]})}
                     for r in results]}
    with open(os.path.join(out, "sounds.json"), "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    with open(os.path.join(out, "index.html"), "w") as f:
        f.write(page(results, commit))

    print(f"\n  {'clip':<11} {'s':>5} {'peak':>6} {'rms dB':>7} {'dc':>8} {'nan':>4} {'over':>5} {'ceil%':>6} {'clicks':>6}  status")
    for r in results:
        m = r.get("m")
        if not m:
            print(f"  {r['id']:<11} no clip: {r['note']}")
            continue
        print(f"  {r['id']:<11} {m['seconds']:5.1f} {m['peak']:6.3f} {m['rms_dbfs']!s:>7} {m['dc']:8.5f} {m['nan_inf']:4d} {m['over_full_scale']:5d} "
              f"{m['near_ceiling_pct']:6.2f} {m['clicks']:6d}  {'ok' if m['ok'] else 'FAIL: ' + '; '.join(m['problems'])}")
    print(f"\n  {len(results) - failed - missing} of {len(results)} ok in {time.monotonic() - t0:.0f} s; {out}")
    return 2 if missing else 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
