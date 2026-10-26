"""Synthetic gestures: the twin's motion model (rig/twin/twin.c: pose_q, sample_at) in numpy, so the tests and the rig
have a hand with known answers. A pose over time (pitch, roll, yaw, shake) becomes the chip's raw samples: gravity and
the body rates in device axes, turned into chip axes by the mounting (an axis map, tune.h motion.ax_*), then the
chip's offset, bias and noise, then LSB. Angles follow rig/PROTOCOL.md: pitch + with the nose (the screen's top edge)
down, roll + with the right edge down. The pose's up vector is the twin's, (-sin r cos p, sin p, cos p cos r): with
pitch and roll both set, motion.c's ACC formula reads the pitch exactly and the roll a little short of r.

    raw = gesture("tilt", 4, pitch=30)                  # int16 (N, 6): ax ay az gx gy gz, chip axes, LSB
    raw, truth = gesture("steps", 18, steps=[(0, 0, 3), (15, 0, 3)], with_truth=True)
    truth = pose_series("sweep", 8, 500)                # t, pitch, roll, yaw, shake, segments
"""
from __future__ import annotations

import json
import os
import subprocess
import tempfile
from datetime import datetime
from pathlib import Path

import numpy as np

GYRO_BIAS = (0.9, -0.6, 0.4)          # deg/s, chip axes (twin.c GBIAS)
ACC_OFFSET = (0.010, -0.006, 0.020)   # g, chip axes (twin.c AOFF)
NOISE_G, NOISE_DPS = 0.004, 0.06      # per sample, rms
MOUNT = (3, 0, 4)                     # the default axis map: device x = -chip y, y = +chip x, z = +chip z
ACC_LSB = (16384.0, 8192.0, 4096.0, 2048.0)
GYRO_LSB = (131.0, 65.5, 32.8, 16.4)
RATES = (100, 250, 500, 1000)
R2D, D2R = 57.2958, 0.0174533         # the firmware's constants
KINDS = ('still', 'tilt', 'steps', 'sweep', 'flicks', 'shake', 'roll', 'axes', 'free')
AXES_POSES = (('flat', 0.0, 0.0), ('nose_down', 60.0, 0.0), ('right_down', 0.0, 60.0), ('facing', -90.0, 0.0))

RIG = Path(__file__).resolve().parent.parent
TWIN = RIG / 'twin' / 'build' / 'twin'


def lsb(acc_range: int = 0, gyro_range: int = 2) -> tuple[float, float]:
    """LSB per g and per deg/s for the tune.h range codes (imu.acc_range, imu.gyro_range)."""
    return ACC_LSB[int(acc_range) & 3], GYRO_LSB[int(gyro_range) & 3]


# ---------------------------------------------------------------- axes
def _map(axis_map) -> tuple[int, int, int]:
    m = tuple(int(e) for e in axis_map)
    if len(m) != 3 or any(e < 0 or e > 5 for e in m):
        raise ValueError(f'axis map {axis_map!r}: three codes 0..5 (0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z)')
    return m


def remap(chip, axis_map=MOUNT) -> np.ndarray:
    """Chip axes to device axes, as motion.c motion_map: device[k] = +/- chip[e >> 1]. Rows of 3."""
    m = _map(axis_map)
    chip = np.asarray(chip, dtype=np.float64)
    out = np.empty_like(chip)
    for k, e in enumerate(m):
        out[..., k] = -chip[..., e >> 1] if e & 1 else chip[..., e >> 1]
    return out


def unmap(dev, axis_map=MOUNT) -> np.ndarray:
    """Device axes back to chip axes (the inverse of remap). The map must use each chip axis once."""
    m = _map(axis_map)
    if len({e >> 1 for e in m}) != 3:
        raise ValueError(f'axis map {axis_map!r} uses a chip axis twice: it has no inverse')
    dev = np.asarray(dev, dtype=np.float64)
    out = np.empty_like(dev)
    for k, e in enumerate(m):
        out[..., e >> 1] = -dev[..., k] if e & 1 else dev[..., k]
    return out


def to_device(raw, acc_lsb: float = ACC_LSB[0], gyro_lsb: float = GYRO_LSB[2], axis_map=MOUNT) -> np.ndarray:
    """Raw chip samples (N, 6) LSB to device axes (N, 6): g and deg/s, nothing removed (motion.csv's columns)."""
    raw = np.asarray(raw, dtype=np.float64).reshape(-1, 6)
    return np.hstack([remap(raw[:, :3] / acc_lsb, axis_map), remap(raw[:, 3:] / gyro_lsb, axis_map)])


def to_raw(dev, acc_lsb: float = ACC_LSB[0], gyro_lsb: float = GYRO_LSB[2], axis_map=MOUNT) -> np.ndarray:
    """Device-axis samples (N, 6) in g and deg/s to the chip's raw LSB (N, 6) int16, rounded and clipped."""
    dev = np.asarray(dev, dtype=np.float64).reshape(-1, 6)
    chip = np.hstack([unmap(dev[:, :3], axis_map) * acc_lsb, unmap(dev[:, 3:], axis_map) * gyro_lsb])
    return np.clip(np.round(chip), -32768, 32767).astype(np.int16)


# ---------------------------------------------------------------- the pose (twin.c pose_q, vectorised)
def pose_q(pitch, roll, yaw) -> np.ndarray:
    """The device's orientation (w, x, y, z): tilt from the up vector, then the yaw about the world's z."""
    th, ph = np.asarray(pitch, np.float64) * D2R, np.asarray(roll, np.float64) * D2R
    ux, uy, uz = -np.sin(ph) * np.cos(th), np.sin(th), np.cos(th) * np.cos(ph)
    n = np.sqrt(ux * ux + uy * uy + uz * uz)
    ux, uy, uz = ux / n, uy / n, uz / n
    a0, a1, a2 = 1 + uz, uy, -ux
    an = np.sqrt(a0 * a0 + a1 * a1 + a2 * a2)
    flip = an < 1e-9                          # upside down: any half turn about a level axis
    an = np.where(flip, 1.0, an)
    a0, a1, a2 = np.where(flip, 0.0, a0 / an), np.where(flip, 1.0, a1 / an), np.where(flip, 0.0, a2 / an)
    h = np.asarray(yaw, np.float64) * D2R * 0.5
    c, s = np.cos(h), np.sin(h)                # (c, 0, 0, s) * a, a's z is 0
    return np.stack([c * a0, c * a1 - s * a2, c * a2 + s * a1, s * a0], axis=-1)


def _qmul(a, b):
    return np.stack([a[..., 0] * b[..., 0] - a[..., 1] * b[..., 1] - a[..., 2] * b[..., 2] - a[..., 3] * b[..., 3],
                     a[..., 0] * b[..., 1] + a[..., 1] * b[..., 0] + a[..., 2] * b[..., 3] - a[..., 3] * b[..., 2],
                     a[..., 0] * b[..., 2] - a[..., 1] * b[..., 3] + a[..., 2] * b[..., 0] + a[..., 3] * b[..., 1],
                     a[..., 0] * b[..., 3] + a[..., 1] * b[..., 2] - a[..., 2] * b[..., 1] + a[..., 3] * b[..., 0]], axis=-1)


def _up(q):
    """Gravity as the accelerometer reads it, device axes (the world's z seen from the device)."""
    return np.stack([2 * (q[..., 1] * q[..., 3] - q[..., 0] * q[..., 2]),
                     2 * (q[..., 0] * q[..., 1] + q[..., 2] * q[..., 3]),
                     q[..., 0] ** 2 - q[..., 1] ** 2 - q[..., 2] ** 2 + q[..., 3] ** 2], axis=-1)


def _ease(e):
    """0 to 1 with a soft start and end (a raised cosine), clipped outside."""
    return 0.5 - 0.5 * np.cos(np.pi * np.clip(e, 0.0, 1.0))


def _take(params: dict, defaults: dict, kind: str) -> dict:
    extra = set(params) - set(defaults)
    if extra:
        raise ValueError(f'{kind}: unknown parameter(s) {sorted(extra)}; it takes {sorted(defaults)}')
    return {**defaults, **params}


def _pose_fn(kind: str, secs: float, rng: np.random.Generator, params: dict):
    """The pose as a function of time, and the holds (segments) a test would label. Angles in degrees, shake in g."""
    z = np.zeros_like
    if kind == 'still':
        p = _take(params, {'pitch': 0.0, 'roll': 0.0, 'yaw': 0.0, 'yaw_rate': 0.0}, kind)
        fn = lambda t: (z(t) + p['pitch'], z(t) + p['roll'], p['yaw'] + p['yaw_rate'] * t, z(t))
        return fn, [{'label': 'still', 't0': 0.0, 't1': secs, 'pitch': p['pitch'], 'roll': p['roll']}]

    if kind in ('tilt', 'roll'):
        if kind == 'roll':                      # roll to an angle, hold, and back
            p = _take(params, {'angle': 35.0, 'start': 0.5, 'ramp': 0.3, 'back': None, 'pitch': 0.0}, kind)
            back = secs - 0.8 if p['back'] is None else p['back']

            def fn(t):
                e = _ease((t - p['start']) / p['ramp']) * (1 - _ease((t - back) / p['ramp']))
                return z(t) + p['pitch'], p['angle'] * e, z(t), z(t)
            return fn, [{'label': 'level', 't0': 0.0, 't1': p['start'], 'pitch': p['pitch'], 'roll': 0.0},
                        {'label': 'hold', 't0': p['start'] + p['ramp'], 't1': back, 'pitch': p['pitch'], 'roll': p['angle']},
                        {'label': 'back', 't0': back + p['ramp'], 't1': secs, 'pitch': p['pitch'], 'roll': 0.0}]
        p = _take(params, {'pitch': 0.0, 'roll': 0.0, 'from_pitch': 0.0, 'from_roll': 0.0, 'start': 0.5, 'ramp': 0.3,
                           'yaw': 0.0}, kind)

        def fn(t):
            e = _ease((t - p['start']) / max(p['ramp'], 1e-6))
            return (p['from_pitch'] + (p['pitch'] - p['from_pitch']) * e,
                    p['from_roll'] + (p['roll'] - p['from_roll']) * e, z(t) + p['yaw'], z(t))
        return fn, [{'label': 'before', 't0': 0.0, 't1': p['start'], 'pitch': p['from_pitch'], 'roll': p['from_roll']},
                    {'label': 'hold', 't0': p['start'] + p['ramp'], 't1': secs, 'pitch': p['pitch'], 'roll': p['roll']}]

    if kind == 'steps':
        p = _take(params, {'steps': [(0, 0, 3), (15, 0, 3), (30, 0, 3)], 'ramp': 0.3, 'from_pitch': 0.0,
                           'from_roll': 0.0}, kind)
        st = [tuple(s) for s in p['steps']]
        starts = np.concatenate([[0.0], np.cumsum([s[2] for s in st])[:-1]])
        segs = []
        for (s, t0) in zip(st, starts):
            label = s[3] if len(s) > 3 else f'pitch {s[0]:g} roll {s[1]:g}'
            segs.append({'label': label, 't0': float(t0 + p['ramp']), 't1': float(t0 + s[2]), 'pitch': float(s[0]),
                         'roll': float(s[1])})

        def fn(t):
            pitch, roll = z(t) + p['from_pitch'], z(t) + p['from_roll']
            prev = (p['from_pitch'], p['from_roll'])
            for (s, t0) in zip(st, starts):
                e = _ease((t - t0) / max(p['ramp'], 1e-6))
                on = t >= t0
                pitch = np.where(on, prev[0] + (s[0] - prev[0]) * e, pitch)
                roll = np.where(on, prev[1] + (s[1] - prev[1]) * e, roll)
                prev = (s[0], s[1])
            return pitch, roll, z(t), z(t)
        return fn, segs

    if kind == 'sweep':
        p = _take(params, {'a': -45.0, 'b': 45.0, 'pre': 0.5, 'post': 0.5, 'roll': 0.0}, kind)
        span = max(secs - p['pre'] - p['post'], 1e-6)
        fn = lambda t: (p['a'] + (p['b'] - p['a']) * np.clip((t - p['pre']) / span, 0, 1), z(t) + p['roll'], z(t), z(t))
        return fn, [{'label': 'start', 't0': 0.0, 't1': p['pre'], 'pitch': p['a'], 'roll': p['roll']},
                    {'label': 'sweep', 't0': p['pre'], 't1': p['pre'] + span, 'pitch': None, 'roll': p['roll'],
                     'rate': (p['b'] - p['a']) / span},
                    {'label': 'end', 't0': p['pre'] + span, 't1': secs, 'pitch': p['b'], 'roll': p['roll']}]

    if kind == 'flicks':                        # quick out-and-back flicks, a raised-cosine pulse each
        p = _take(params, {'n': 5, 'amp': 30.0, 'width': 0.15, 'axis': 'pitch', 'pitch': 0.0, 'roll': 0.0,
                           'start': None, 'gap': None}, kind)
        n = int(p['n'])
        gap = secs / (n + 1) if p['gap'] is None else p['gap']
        first = gap if p['start'] is None else p['start']
        cs = first + gap * np.arange(n)
        w = p['width']

        def fn(t):
            f = z(t)
            for c in cs:
                e = (t - (c - w / 2)) / w
                f = f + np.where((e >= 0) & (e <= 1), 0.5 - 0.5 * np.cos(2 * np.pi * np.clip(e, 0, 1)), 0.0)
            f = p['amp'] * f
            if p['axis'] == 'roll':
                return z(t) + p['pitch'], p['roll'] + f, z(t), z(t)
            return p['pitch'] + f, z(t) + p['roll'], z(t), z(t)
        return fn, [{'label': f'flick {i + 1}', 't0': float(c - w / 2), 't1': float(c + w / 2), 'peak_t': float(c),
                     'amp': p['amp'], 'peak_dps': float(np.pi * p['amp'] / w)} for i, c in enumerate(cs)]

    if kind == 'shake':                         # shaking without turning: the twin's 7, 9 and 11 Hz on x, y, z
        p = _take(params, {'amp': 0.5, 'start': 0.5, 'end': None, 'ramp': 0.3, 'pitch': 0.0, 'roll': 0.0}, kind)
        end = secs - 0.5 if p['end'] is None else p['end']
        fn = lambda t: (z(t) + p['pitch'], z(t) + p['roll'], z(t),
                        p['amp'] / 1.5 * _ease((t - p['start']) / p['ramp']) * _ease((end - t) / p['ramp']))
        return fn, [{'label': 'rest', 't0': 0.0, 't1': p['start'], 'pitch': p['pitch'], 'roll': p['roll']},
                    {'label': 'shake', 't0': p['start'] + p['ramp'], 't1': end - p['ramp'], 'pitch': p['pitch'],
                     'roll': p['roll'], 'amp': p['amp']},
                    {'label': 'rest after', 't0': end, 't1': secs, 'pitch': p['pitch'], 'roll': p['roll']}]

    if kind == 'axes':                          # each pose held: flat, nose down, right edge down, facing the player
        p = _take(params, {'hold': None, 'ramp': 0.6, 'poses': AXES_POSES}, kind)
        poses = [tuple(x) for x in p['poses']]
        hold = secs / len(poses) if p['hold'] is None else p['hold']
        starts = hold * np.arange(len(poses))
        steps = [(pp, rr, hold) for (_, pp, rr) in poses]
        segs = [{'label': lab, 't0': float(t0 + (p['ramp'] if i else 0.0)), 't1': float(t0 + hold), 'pitch': pp, 'roll': rr}
                for i, ((lab, pp, rr), t0) in enumerate(zip(poses, starts))]
        f, _ = _pose_fn('steps', secs, rng, {'steps': steps, 'ramp': p['ramp'], 'from_pitch': poses[0][1],
                                             'from_roll': poses[0][2]})
        return f, segs

    if kind == 'free':                          # a smooth random performance, the same for the same seed
        p = _take(params, {'pitch_amp': 35.0, 'roll_amp': 25.0, 'yaw_amp': 30.0, 'fade': 1.0}, kind)
        fp, fr, fy = rng.uniform(0.08, 1.2, 5), rng.uniform(0.08, 0.9, 4), rng.uniform(0.02, 0.1, 2)
        ap, ar = rng.uniform(0.3, 1.0, 5), rng.uniform(0.3, 1.0, 4)
        ap, ar = ap / ap.sum() * 1.6, ar / ar.sum() * 1.4
        php, phr, phy = rng.uniform(0, 2 * np.pi, 5), rng.uniform(0, 2 * np.pi, 4), rng.uniform(0, 2 * np.pi, 2)

        def fn(t):
            env = _ease(t / p['fade']) * _ease((secs - t) / p['fade'])
            sp = sum(a * np.sin(2 * np.pi * f * t + h) for a, f, h in zip(ap, fp, php))
            sr = sum(a * np.sin(2 * np.pi * f * t + h) for a, f, h in zip(ar, fr, phr))
            sy = sum(np.sin(2 * np.pi * f * t + h) for f, h in zip(fy, phy)) / 2
            return p['pitch_amp'] * np.tanh(sp) * env, p['roll_amp'] * np.tanh(sr) * env, p['yaw_amp'] * sy, z(t)
        return fn, [{'label': 'play', 't0': 0.0, 't1': secs}]

    raise ValueError(f'unknown gesture {kind!r}: one of {", ".join(KINDS)}')


def pose_series(kind: str, secs: float, rate_hz: float = 500, seed: int = 0, **params) -> dict:
    """The truth behind gesture(): t, pitch, roll, yaw (deg) and shake (g), one per sample, and the labelled holds."""
    n = int(round(secs * rate_hz))
    t = np.arange(n) / float(rate_hz)
    fn, segs = _pose_fn(kind, float(secs), np.random.default_rng([seed, 0]), params)
    pitch, roll, yaw, shake = (np.asarray(x, np.float64) for x in fn(t))
    return {'kind': kind, 't': t, 'pitch': pitch, 'roll': roll, 'yaw': yaw, 'shake': shake, 'segments': segs}


gesture_truth = pose_series


def gesture(kind: str, secs: float, rate_hz: float = 500, acc_range: int = 0, gyro_range: int = 2, seed: int = 0,
            gyro_bias=GYRO_BIAS, acc_offset=ACC_OFFSET, noise_g: float = NOISE_G, noise_dps: float = NOISE_DPS,
            mount=MOUNT, with_truth: bool = False, **params):
    """A gesture as the chip would sample it: int16 (N, 6), ax ay az gx gy gz in the chip's axes and LSB, mounted as
    `mount` (the axis map the firmware needs for it). Bias and offset are in chip axes. Kinds and their parameters:

    still (pitch, roll, yaw, yaw_rate); tilt (pitch, roll, from_pitch, from_roll, start, ramp); steps (steps=[(pitch,
    roll, secs[, label])...], ramp); sweep (a, b, pre, post); flicks (n, amp, width, axis); shake (amp g, start, end);
    roll (angle, start, back); axes (hold, poses); free (pitch_amp, roll_amp, yaw_amp)."""
    n = int(round(secs * rate_hz))
    dt = 1.0 / float(rate_hz)
    t = np.arange(n) / float(rate_hz)
    fn, segs = _pose_fn(kind, float(secs), np.random.default_rng([seed, 0]), params)
    p0, r0, y0, sh = (np.asarray(x, np.float64) for x in fn(t))
    p1, r1, y1, _ = (np.asarray(x, np.float64) for x in fn(t + dt))
    q0, q1 = pose_q(p0, r0, y0), pose_q(p1, r1, y1)
    dq = _qmul(q0 * np.array([1, -1, -1, -1]), q1)
    dq = np.where(dq[:, :1] < 0, -dq, dq)
    w = 2 * dq[:, 1:] / dt * R2D                          # body rates, deg/s, device axes
    up = _up(q0)
    k = np.arange(3)
    up = up + (sh[:, None] * np.sin(t[:, None] * 2 * np.pi * (7.0 + 2.0 * k)) * 1.5)
    al, gl = lsb(acc_range, gyro_range)
    rng = np.random.default_rng([seed, 1])
    a = (unmap(up, mount) + np.asarray(acc_offset, np.float64) + noise_g * rng.standard_normal((n, 3))) * al
    g = (unmap(w, mount) + np.asarray(gyro_bias, np.float64) + noise_dps * rng.standard_normal((n, 3))) * gl
    raw = np.clip(np.round(np.hstack([a, g])), -32768, 32767).astype(np.int16)
    if with_truth:
        return raw, {'kind': kind, 't': t, 'pitch': p0, 'roll': r0, 'yaw': y0, 'shake': sh, 'segments': segs}
    return raw


# ---------------------------------------------------------------- a session folder (rig/PROTOCOL.md, section 3)
def write_session(folder, raw, rate_hz: float = 500, acc_range: int = 0, gyro_range: int = 2, axis_map=MOUNT,
                  state: dict | None = None, events: list | None = None, test: dict | None = None,
                  tune: dict | None = None, settings: dict | None = None, name: str | None = None,
                  source: str = 'synth', notes: str = '', flags=None) -> Path:
    """Write a session as the daemon does: meta.json, motion.csv (device axes by `axis_map`, the map the board had),
    raw.txt, events.jsonl and, given columns (t plus state fields), state.csv. Returns the folder."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    raw = np.asarray(raw).reshape(-1, 6).astype(np.int16)
    n = len(raw)
    al, gl = lsb(acc_range, gyro_range)
    t = np.arange(n) / float(rate_hz)
    dev = to_device(raw, al, gl, axis_map)
    fl = np.zeros(n, dtype=int) + 4 if flags is None else np.asarray(flags, dtype=int)
    with open(folder / 'motion.csv', 'w') as f:
        f.write('t,ax,ay,az,gx,gy,gz,flags\n')
        if n:
            np.savetxt(f, np.column_stack([t, dev, fl]), delimiter=',',
                       fmt=['%.5f', '%.5f', '%.5f', '%.5f', '%.3f', '%.3f', '%.3f', '%d'])
    with open(folder / 'raw.txt', 'w') as f:
        if n:
            np.savetxt(f, raw, fmt='%d')
    tv = dict(tune or {})
    for k, e in zip(('motion.ax_x', 'motion.ax_y', 'motion.ax_z'), axis_map):
        tv.setdefault(k, int(e))
    tv.setdefault('imu.acc_range', int(acc_range))
    tv.setdefault('imu.gyro_range', int(gyro_range))
    meta = {'name': name or folder.name, 'started': datetime.now().isoformat(timespec='seconds'),
            'secs': round(n / float(rate_hz), 3), 'source': source,
            'board': {'name': 'synth', 'serial': '', 'version': ''}, 'tune': tv, 'settings': settings or {},
            'imu': {'rate_hz': rate_hz, 'acc_lsb': al, 'gyro_lsb': gl, 'axis_map': [int(e) for e in axis_map],
                    'acc_range': int(acc_range), 'gyro_range': int(gyro_range)},
            'test': test, 'injected': False, 'notes': notes}
    (folder / 'meta.json').write_text(json.dumps(meta, indent=1))
    with open(folder / 'events.jsonl', 'w') as f:
        for e in events or []:
            f.write(json.dumps({'t': e.get('t', 0), 'name': e['name'], 'v': e.get('v', 0), 'ms': e.get('ms', 0),
                                'text': e.get('text', '')}) + '\n')
    if state:
        cols = ['t'] + [c for c in state if c != 't']
        data = np.column_stack([np.asarray(state[c], dtype=np.float64) for c in cols])
        with open(folder / 'state.csv', 'w') as f:
            f.write(','.join(cols) + '\n')
            np.savetxt(f, data, delimiter=',', fmt='%.6g')
    return folder


# ---------------------------------------------------------------- the twin's calculator (the firmware's own C)
def twin_path() -> Path:
    return Path(os.environ.get('SLRIG_TWIN', TWIN))


def _calc(lines: list[str], twin=None, timeout: float = 60) -> list[str]:
    exe = Path(twin) if twin else twin_path()
    if not exe.exists():
        raise FileNotFoundError(f'no twin at {exe}: build it with rig/twin/build.sh')
    out = subprocess.run([str(exe), '--calc'], input='\n'.join(lines + ['quit']) + '\n', capture_output=True,
                         text=True, timeout=timeout)
    if out.returncode != 0:
        raise RuntimeError(f'twin --calc failed: {out.stderr.strip()[:300]}')
    return out.stdout.splitlines()


def _sets(tune: dict | None) -> list[str]:
    from .analysis import tune_table              # the table's order comes from tune.h
    idx = {d['name']: d['i'] for d in tune_table()}
    lines = []
    for name, v in (tune or {}).items():
        if name not in idx:
            raise KeyError(f'no tuning value {name!r} in tune.h')
        lines.append(f'set {idx[name]} {float(v):.6g}')
    return lines


def twin_states(raw, rate_hz: float = 500, acc_range: int = 0, gyro_range: int = 2, tune: dict | None = None,
                spd_min: float = 0.05, spd_max: float = 2.0, twin=None) -> dict:
    """The board's control ticks for these raw samples, from the firmware's motion.c and gesture.c in the twin
    (`twin --calc replay`), as state.csv columns: t, pitch, roll, yaw, speed_cmd, tilt_u, zone, detent, delay, stutter,
    flags (64 IMU, 1024 rest), bias_x/y/z (chip axes), qw..qz. t is the time of the tick's last sample."""
    raw = np.asarray(raw).reshape(-1, 6)
    al, gl = lsb(acc_range, gyro_range)
    with tempfile.TemporaryDirectory(prefix='slrig-twin-') as d:
        fin, fout = Path(d) / 'raw.txt', Path(d) / 'replay.csv'
        np.savetxt(fin, raw, fmt='%d')
        lines = _sets(tune) + [f'replay {fin} {fout} {int(rate_hz)} {al:g} {gl:g} {spd_min:g} {spd_max:g}']
        out = _calc(lines, twin)
        if not any(x.startswith('ok') for x in out[-1:]):
            raise RuntimeError(f'twin replay: {out[-1:] or "no answer"}')
        rows = np.loadtxt(fout, delimiter=',', skiprows=1, ndmin=2)
    cols = 't,pitch,roll,yaw,speed,u,zone,detent,delay,stutter,rest,bias_x,bias_y,bias_z,qw,qx,qy,qz'.split(',')
    r = {c: rows[:, i] for i, c in enumerate(cols)}
    return {'t': r['t'] - 1.0 / float(rate_hz), 'state': np.full(len(rows), 2.0), 'mode': np.zeros(len(rows)),
            'flags': 64 + 1024 * r['rest'].astype(int), 'qw': r['qw'], 'qx': r['qx'], 'qy': r['qy'], 'qz': r['qz'],
            'pitch': r['pitch'], 'roll': r['roll'], 'yaw': r['yaw'], 'tilt_u': r['u'], 'zone': r['zone'],
            'detent': r['detent'], 'speed_cmd': r['speed'], 'delay': r['delay'], 'stutter': r['stutter'],
            'bias_x': r['bias_x'], 'bias_y': r['bias_y'], 'bias_z': r['bias_z'], 'rate_hz': np.full(len(rows), rate_hz)}


def twin_curve(tune: dict | None = None, n: int = 121, spd_min: float = 0.05, spd_max: float = 2.0, twin=None) -> dict:
    """The tilt and roll curves from the twin (`curve` in --calc), as the daemon's `curve` command returns them."""
    out = _calc(_sets(tune) + [f'curve {int(n)} {spd_min:g} {spd_max:g}'], twin)
    return json.loads(out[-1])
