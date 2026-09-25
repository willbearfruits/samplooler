#!/usr/bin/env python3
"""The showcase performance: the riff someone plays into the mic, and what they do with the box.

Writes, into the work dir given as argv[1]:
  mic.f32        what the microphone hears (32 kHz mono float32), the whole video long
  score.txt      one line per 10 ms tick for the twin: button, mode, pitch, roll, grain pitch, toast
  timeline.json  video sections, captions, and the box's tilt per video frame for the 3D shot
"""
import json
import math
import os
import sys

import numpy as np

SR = 32000
FPS = 24
BPM = 480 / 4.36                   # two bars = 4.36 s: a whole number of the firmware's 10 ms ticks
BEAT = 60.0 / BPM
BAR = 4 * BEAT
CYCLE = 2 * BAR                       # the riff: two bars

# ------------------------------------------------------------------ video sections (crossfade X)
X = 0.5
SECTIONS = [('title', 3.0), ('perf', 37.5), ('stack', 265 / FPS), ('mini', 241 / FPS), ('copper', 6.6),
            ('case', 8.0), ('software', 10.0), ('end', 6.0)]
starts, t = {}, 0.0
for name, d in SECTIONS:
    starts[name] = t
    t += d - X
TOTAL = t + X
P0 = starts['perf']                    # the performance shot starts here in the final video


# ------------------------------------------------------------------ the riff
rng = np.random.default_rng(7)


def env(n, a, d, sr=SR):
    t = np.arange(n) / sr
    e = np.exp(-t / d)
    na = max(1, int(a * sr))
    e[:na] *= np.linspace(0, 1, na)
    return e


def onepole_lp(x, fc):
    """fc: array or scalar cutoff in Hz."""
    fc = np.broadcast_to(np.asarray(fc, dtype=float), x.shape)
    a = 1.0 - np.exp(-2 * np.pi * fc / SR)
    y = np.empty_like(x)
    s = 0.0
    for i in range(len(x)):
        s += a[i] * (x[i] - s)
        y[i] = s
    return y


def kick(n):
    t = np.arange(n) / SR
    f = 44 + 90 * np.exp(-t * 28)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph) * np.exp(-t * 7.5) * 0.9 + rng.standard_normal(n) * np.exp(-t * 400) * 0.08


def snare(n):
    t = np.arange(n) / SR
    noise = rng.standard_normal(n)
    noise = np.diff(noise, prepend=0) * 0.6
    body = np.sin(2 * np.pi * 186 * t) * np.exp(-t * 30)
    return (noise * np.exp(-t * 16) * 0.42 + body * 0.38)


def hat(n, open_=False):
    t = np.arange(n) / SR
    noise = rng.standard_normal(n)
    noise = np.diff(np.diff(noise, prepend=0), prepend=0) * 0.3
    return noise * np.exp(-t * (11 if open_ else 55)) * 0.3


def hz(semis_from_a4):
    return 440.0 * 2 ** (semis_from_a4 / 12)


def bass(n, f):
    t = np.arange(n) / SR
    saw = 2 * ((f * t) % 1.0) - 1
    sq = np.sign(np.sin(2 * np.pi * f * t)) * 0.4
    x = saw * 0.7 + sq
    cut = 180 + 1400 * np.exp(-t * 14)
    y = onepole_lp(onepole_lp(x, cut), cut)
    return y * env(n, 0.004, 0.35) * 0.55


def ep(n, f, bright=1.0):
    """FM electric piano."""
    t = np.arange(n) / SR
    idx = (1.6 * bright) * np.exp(-t * 5)
    y = np.sin(2 * np.pi * f * t + idx * np.sin(2 * np.pi * f * t))
    y += 0.25 * np.sin(2 * np.pi * 2 * f * t) * np.exp(-t * 8)
    return y * env(n, 0.003, 0.9) * (1 + 0.08 * np.sin(2 * np.pi * 5.2 * t))


def bell(n, f):
    t = np.arange(n) / SR
    idx = 2.2 * np.exp(-t * 7)
    y = np.sin(2 * np.pi * f * t + idx * np.sin(2 * np.pi * 3.5 * f * t))
    return y * env(n, 0.002, 0.55)


def riff():
    n = int(round(CYCLE * SR))
    out = np.zeros(n + SR)                          # room for tails; folded back at the end
    s16 = BEAT / 4

    def put(x, step, gain=1.0):
        i = int(round(step * s16 * SR))
        out[i:i + len(x)] += x[:len(out) - i] * gain

    # drums
    for st in (0, 7, 10, 16, 23, 26, 29):
        put(kick(int(0.45 * SR)), st, 1.0)
    for st in (4, 12, 20, 28):
        put(snare(int(0.3 * SR)), st, 1.0)
    put(snare(int(0.2 * SR)), 31, 0.35)             # ghost
    for st in range(0, 32, 2):
        put(hat(int(0.12 * SR), open_=st in (14, 30)), st, 0.9 if st % 4 == 0 else 0.6)
    for st in range(1, 32, 4):
        put(hat(int(0.05 * SR)), st + 2, 0.25)
    # bass: A minor, from A1
    for st, semi, ln in ((0, -36, 3), (4, -36, 2), (6, -33, 2), (8, -31, 3), (12, -29, 2), (14, -31, 2),
                         (16, -33, 3), (20, -33, 2), (22, -36, 2), (24, -38, 3), (28, -36, 4)):
        put(bass(int(ln * s16 * SR * 1.05), hz(semi)), st, 1.0)
    # keys: Am9 and Fmaj7 stabs
    am9, fmaj7, g = (-12, -9, -5, -2, 2), (-16, -12, -9, -5), (-14, -10, -7)
    for st, chord, ln, g_ in ((0, am9, 5, 0.9), (10, am9, 2, 0.6), (16, fmaj7, 5, 0.9), (24, g, 3, 0.7), (27, g, 3, 0.5)):
        for semi in chord:
            put(ep(int(ln * s16 * SR * 1.3), hz(semi)), st, 0.11 * g_)
    # the hook: a bell line
    for st, semi, ln in ((2, 7, 2), (6, 10, 2), (8, 12, 3), (11, 10, 1), (12, 7, 3),
                         (18, 5, 2), (20, 7, 2), (22, 3, 2), (24, 0, 6)):
        put(bell(int(ln * s16 * SR * 1.6), hz(semi)), st, 0.16)
    loop = out[:n].copy()
    loop[:len(out) - n] += out[n:]                  # tails wrap round, as a played loop would
    loop = np.tanh(loop * 1.1) / 1.1
    return loop / np.max(np.abs(loop)) * 0.62


# ------------------------------------------------------------------ the performance
R0 = 0.35                                          # the player starts the riff here (video time)
T_REC = R0 + CYCLE                                 # record the second time round, from the bar line
DEB = 0.02                                         # main.c's debounce: held on the 3rd tick down
T_DOWN = T_REC - DEB
T_UP = T_DOWN + CYCLE + 0.010                      # +10 ms: the seam crossfade eats that much
T_LOOP = T_UP + DEB                                # the loop starts here


def ramp(keys, t):
    """Piecewise smoothstep through (time, value) keys."""
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t0 <= t <= t1:
            u = (t - t0) / (t1 - t0) if t1 > t0 else 1.0
            u = u * u * (3 - 2 * u)
            return v0 + (v1 - v0) * u
    return keys[-1][1]


ONE_X = 30.27         # 1.00x: solved from main.c's curve (0.05x flat .. 2x at 45 degrees)
HALF_X = 21.5         # 0.50x
S2, S3, S4, S5, S6, S7 = (starts[k] for k in ('stack', 'mini', 'copper', 'case', 'software', 'end'))

PITCH = [(0, 0), (T_LOOP - 0.8, 0), (T_LOOP - 0.1, ONE_X),
         (13.4, ONE_X), (14.3, 45), (16.0, 45), (16.8, ONE_X),
         (17.6, ONE_X), (19.0, -ONE_X), (21.0, -ONE_X), (21.6, 0),
         (23.0, 0), (24.4, ONE_X),                                   # the tape spins back up
         (33.8, ONE_X), (34.6, 2.5),                                 # grain mode: flat, frozen
         (38.4, 2.5), (39.2, ONE_X),                                 # back to tape, 1x
         (S3 + 1.0, ONE_X), (S3 + 1.8, HALF_X), (S3 + 6.5, HALF_X), (S3 + 7.3, ONE_X),
         (S5 + 0.4, ONE_X), (S5 + 1.2, 3.0), (S6 + 0.2, 3.0), (S6 + 0.9, ONE_X),
         (S7 + 0.6, ONE_X), (S7 + 3.4, 0.0)]
ROLL = [(0, 0), (25.8, 0), (26.4, -40), (29.0, -40), (29.6, 0),
        (30.2, 0), (30.6, 30), (31.8, 30), (32.4, 43), (33.0, 43), (33.4, 0),
        (S2 + 0.5, 0), (S2 + 1.3, -24), (S3 - 0.5, -24), (S3 + 0.3, 0),
        (S6 + 0.5, 0), (S6 + 1.3, -22), (S7 + 3.0, -22), (S7 + 3.6, -30)]
PITCH_ST = [(0, 0), (35.0, 0), (36.0, 12), (37.0, 12), (37.8, 0),
            (S5 + 1.5, 0), (S5 + 2.5, 7), (S5 + 6.5, 7), (S5 + 7.3, 0)]
MODE = [(0, 0), (33.8, 1), (38.5, 0), (S5 + 0.8, 1), (S6 + 0.4, 0)]     # 0 TAPE, 1 GRAIN
TOASTS = [(33.8, 'Mode', 'GRAIN', 1.0), (38.5, 'Mode', 'TAPE', 0.0)]

CAPTIONS = [   # (start, end, headline, detail): shown during the performance shot
    (3.2, T_DOWN - 0.1, 'Samplooler', 'a looper you play by tilting it'),
    (T_DOWN - 0.1, T_LOOP, 'Hold to record', 'the mic goes into a 20 s buffer, and straight through to the output'),
    (T_LOOP, 13.3, 'Let go: it loops', 'the seam is crossfaded, the loop never clicks'),
    (13.4, 17.4, 'Nose down: faster', 'tilt sets the speed: 1x at 30°, 2x at 45°'),
    (17.5, 21.5, 'Nose up: backwards', 'the same curve, in reverse'),
    (21.6, 25.2, 'Flat: it crawls', '0.05x, so the sound never quite stops'),
    (25.6, 30.0, 'Roll left: tape delay', 'the angle sets the feed and how long the tail rings'),
    (30.1, 33.7, 'Roll right: stutter', 'grabs a slice and loops it, shorter as you tilt further'),
    (33.8, 38.6, 'Grain mode', 'speed and pitch apart: freeze the sound, shift it an octave'),
]


def write(work):
    os.makedirs(work, exist_ok=True)
    loop = riff()
    n = int(TOTAL * SR) + SR
    mic = np.zeros(n, dtype=np.float32)
    for k in range(2):                             # the player plays it twice, then stops
        i = int(round((R0 + k * CYCLE) * SR))
        mic[i:i + len(loop)] += loop.astype(np.float32)
    mic.tofile(os.path.join(work, 'mic.f32'))
    np.save(os.path.join(work, 'riff.npy'), loop.astype(np.float32))

    ticks = int(TOTAL * 100) + 50
    lines, toast, tname, tval, tfrac = [], 0, '', '', -1.0
    events = sorted(TOASTS)
    for k in range(ticks):
        t = k / 100.0
        down = 1 if T_DOWN <= t < T_UP else 0
        mode = [m for (tm, m) in MODE if tm <= t][-1]
        while events and events[0][0] <= t:
            _, tname, tval, tfrac = events.pop(0)
            toast = 1500
        toast = max(0, toast - 10)
        lines.append(f'{down} {mode} {ramp(PITCH, t):.3f} {ramp(ROLL, t):.3f} {ramp(PITCH_ST, t):.3f} '
                     f'{toast}|{tname or "Mode"}|{tval or "TAPE"}|{tfrac}\n')
    open(os.path.join(work, 'score.txt'), 'w').writelines(lines)

    frames = int(round(dict(SECTIONS)['perf'] * FPS))
    angles = [(round(ramp(PITCH, P0 + f / FPS), 3), round(ramp(ROLL, P0 + f / FPS), 3)) for f in range(frames)]
    json.dump(dict(fps=FPS, sr=SR, total=TOTAL, crossfade=X, sections=[dict(name=nm, start=starts[nm], dur=d)
                                                                            for nm, d in SECTIONS],
                   perf_t0=P0, t_rec=T_REC, t_loop=T_LOOP, captions=CAPTIONS, angles=angles, bpm=BPM),
              open(os.path.join(work, 'timeline.json'), 'w'), indent=1)
    print(f'total {TOTAL:.2f} s, performance {P0:.2f}..{P0 + dict(SECTIONS)["perf"]:.2f}, '
          f'record {T_REC:.3f}..{T_UP + DEB:.3f}, loop {CYCLE:.4f} s')


if __name__ == '__main__':
    write(sys.argv[1])
