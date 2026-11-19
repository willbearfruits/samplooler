#!/usr/bin/env python3
"""The instruments' host speed budgets, measured several times on one quiet core, and what they suggest for the S3.

  tools/budgets.py                    BAND and SING, 5 runs each, pinned to the last CPU
  tools/budgets.py band --runs 9      one instrument, more runs
  tools/budgets.py --cpu 3            another core

Each run is the instrument's own simulator (`tools/sim/run.sh inst`, then `./sim inst` again), which times
`front_audio()` over 3000 blocks of its busiest case and writes the numbers to out/results.json. This tool only
repeats those runs and reads the numbers back: the checks, budgets and scenarios are the tests' own.

What a host number is not: the board's cost. docs/CORE.md's rule is about 70x the host (JAM, 2026-10-14: 14 us here,
0.8 to 1.1 ms of the 2 ms block on the S3 with the audio in IRAM, so 57x to 79x). It holds only with the audio code in
IRAM and the hot buffers in internal RAM: a cache collision in PSRAM cost SING 2000 us on the board while the host
saw 6. The host times the mean block; the board's worst block is what drops audio. Confirm on a board with
`sl_midi.py status` (the prof: lines) or the instrument's `cpu us/block` log.
"""
import json
import os
import statistics
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLOCK_US = 64 / 32000 * 1e6            # one 64-frame block at 32 kHz: 2000 us
RATIO = (57, 70, 79)                    # board us per host us: JAM's low, CORE.md's rule, JAM's high

# metric -> (what, budget in host us, whole instrument?). The budgets are the ones the tests check.
METRICS = {
    "band": [("band_us_per_block", "the whole band, busiest", 30, True),
             ("drums_us_per_block", "DRUMS", 9, False),
             ("bass", "BASS", 3, False),             # test_bass.c has no metric(): read from its check line
             ("lead_us_per_block", "LEAD", 3, False),
             ("keys_us_per_block", "KEYS", 5, False),
             ("breath_us_per_block_live_harmony", "BREATH live + harmony", 7, False),
             ("howl_live_us_per_block", "HOWL live", 6, False)],
    "sing": [("sing_us_per_block", "everything at once", 20, True)],
}


def read(inst):
    d = json.load(open(os.path.join(ROOT, "instruments", inst, "tools/sim/out/results.json")))
    got = {r["metric"]: r["value"] for r in d["results"] if "metric" in r}
    for r in d["results"]:
        c = r.get("check", "")
        if c.startswith("the bass takes "):
            got["bass"] = float(c.split()[3])
    return got, d.get("failed", 0)


def main():
    args = sys.argv[1:]
    runs, cpu = 5, os.cpu_count() - 1
    if "--runs" in args:
        i = args.index("--runs"); runs = int(args[i + 1]); del args[i:i + 2]
    if "--cpu" in args:
        i = args.index("--cpu"); cpu = int(args[i + 1]); del args[i:i + 2]
    insts = args or ["band", "sing"]
    load = os.getloadavg()[0]
    print(f"{runs} runs each on CPU {cpu}; load average {load:.1f} on {os.cpu_count()} CPUs at the start")
    for inst in insts:
        simdir = os.path.join(ROOT, "instruments", inst, "tools/sim")
        vals, fails = {}, []
        for k in range(runs):
            cmd = ["taskset", "-c", str(cpu)] + (["./run.sh", "inst"] if k == 0 else ["./sim", "inst"])
            subprocess.run(cmd, cwd=simdir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            got, failed = read(inst)
            fails.append(failed)
            for m, *_ in METRICS[inst]:
                if m in got:
                    vals.setdefault(m, []).append(got[m])
        print(f"\n{inst.upper()}  (failed checks per run: {fails})")
        print(f"  {'':28} {'budget':>6} {'min':>6} {'median':>6} {'max':>6}  over budget")
        for m, what, budget, whole in METRICS[inst]:
            v = vals.get(m)
            if not v:
                print(f"  {what:28} {budget:6} no number"); continue
            over = sum(x > budget for x in v)
            med = statistics.median(v)
            print(f"  {what:28} {budget:6} {min(v):6.2f} {med:6.2f} {max(v):6.2f}  {over} of {len(v)}  "
                  f"(median {100 * med / budget:.0f} % of the budget)")
            if whole:
                est = ", ".join(f"{r}x {100 * med * r / BLOCK_US:.0f} %" for r in RATIO)
                print(f"  {'':28} on the S3, by the host ratio: {est} of the 2 ms block "
                      f"(the budget itself is {100 * budget * RATIO[1] / BLOCK_US:.0f} % at 70x)")


if __name__ == "__main__":
    main()
