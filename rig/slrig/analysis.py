"""Analysis of recorded sessions (rig/PROTOCOL.md, section 3). load_session() reads a folder into numpy arrays; every
analyser below is a pure function that returns a plain dict (JSON-ready, rounded): `metrics` (numbers), `findings`
({"metric", "value", "note"} in plain words) and `suggest` ({tune name: value}, only when the data clearly supports
it). Axes are the device's (right-handed: x to the right edge, y to the top edge, the nose, z out of the screen)
unless a name says chip.
Tuning values that live in chip axes (imu.gyro_bias_*, imu.acc_off_*) are converted back with the session's axis map.
"""
from __future__ import annotations

import csv
import json
import math
import re
import struct
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path

import numpy as np

from . import synth

FW = Path(__file__).resolve().parents[2] / 'firmware' / 'main'
FLAG_BITS = {'frozen': 1, 'menu': 2, 'injected': 4, 'rec_down': 8, 'enc_down': 16, 'joy_present': 32,
             'imu_present': 64, 'recording': 128, 'playing': 256, 'joy_override': 512, 'rest': 1024, 'menu_edit': 2048}
ZONES = ('dead', 'fwd', 'back')
AXES = ('x', 'y', 'z')
CODES = ('+x', '-x', '+y', '-y', '+z', '-z')
TUNE_FALLBACK = {                       # used only when tune.h cannot be read
    'imu.acc_range': 0, 'imu.gyro_range': 2, 'imu.rate': 2, 'imu.acc_off_x': 0, 'imu.acc_off_y': 0, 'imu.acc_off_z': 0,
    'imu.gyro_bias_x': 0, 'imu.gyro_bias_y': 0, 'imu.gyro_bias_z': 0, 'imu.auto_bias': 1, 'imu.rest_gyro': 3,
    'motion.fusion': 0, 'motion.ax_x': 3, 'motion.ax_y': 1, 'motion.ax_z': 5, 'tilt.sign': 1, 'tilt.dead': 5,
    'tilt.full': 45, 'tilt.hyst': 0, 'roll.on': 1, 'roll.sign': 1, 'roll.dead': 15, 'roll.full': 45, 'joy.dead': 0.12,
    'joy.flick': 0.7, 'joy.rearm': 0.3, 'enc.dir': 0, 'enc.steps': 4, 'enc.tap_min': 40, 'enc.hold': 500,
    'enc.long': 3000, 'enc.double': 300, 'enc.accel': 0, 'rec.debounce': 3}


# ---------------------------------------------------------------- the tuning table, read from the firmware
@lru_cache(maxsize=1)
def tune_table() -> tuple:
    """Every tuning value in tune.h's order: {i, name, kind, min, max, step, def, unit, labels, help}."""
    rx = re.compile(r'^X\((\w+),\s*"([^"]*)",\s*(TK_\w+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*"([^"]*)",'
                    r'\s*"([^"]*)",\s*"((?:[^"\\]|\\.)*)"\)')
    num = lambda s: float(s.strip().rstrip('fF'))
    out = []
    try:
        text = (FW / 'tune.h').read_text()
    except OSError:
        return tuple({'i': i, 'name': n, 'kind': '', 'min': None, 'max': None, 'step': None, 'def': v, 'unit': '',
                      'labels': [], 'help': ''} for i, (n, v) in enumerate(TUNE_FALLBACK.items()))
    for line in text.splitlines():
        m = rx.match(line.strip())
        if m:
            out.append({'i': len(out), 'name': m.group(2), 'kind': m.group(3)[3:].lower(), 'min': num(m.group(4)),
                        'max': num(m.group(5)), 'step': num(m.group(6)), 'def': num(m.group(7)), 'unit': m.group(8),
                        'labels': m.group(9).split('|') if m.group(9) else [], 'help': m.group(10)})
    return tuple(out)


def tune_default(name: str):
    for d in tune_table():
        if d['name'] == name:
            return d['def']
    return TUNE_FALLBACK.get(name)


def _snap(name: str, v: float) -> float:
    """A suggested value on the table's own grid and range."""
    for d in tune_table():
        if d['name'] == name and d['step']:
            v = min(max(v, d['min']), d['max'])
            v = round(round(v / d['step']) * d['step'], 6)
            return int(v) if d['kind'] in ('int', 'enum', 'bool') else v
    return v


# ---------------------------------------------------------------- small helpers
def _f(x, nd: int = 3):
    """A plain float rounded to nd places; None for NaN or nothing."""
    if x is None:
        return None
    try:
        x = float(x)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(x):
        return None
    return round(x, nd)


def _v(xs, nd: int = 3) -> list:
    return [_f(x, nd) for x in np.asarray(xs, dtype=np.float64).ravel()]


def jsonable(o):
    """numpy scalars and arrays to plain Python, NaN to None, recursively."""
    if isinstance(o, dict):
        return {str(k): jsonable(v) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [jsonable(v) for v in o]
    if isinstance(o, np.ndarray):
        return jsonable(o.tolist())
    if isinstance(o, (np.bool_, bool)):
        return bool(o)
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (float, np.floating)):
        return None if not math.isfinite(float(o)) else float(o)
    return o


def _finding(metric: str, value, note: str) -> dict:
    return {'metric': metric, 'value': value, 'note': note}


def _result(metrics: dict, findings: list, suggest: dict | None = None, **more) -> dict:
    return jsonable({**more, 'metrics': metrics, 'findings': findings, 'suggest': suggest or {}})


def _win(t: np.ndarray, t0, t1) -> np.ndarray:
    m = np.ones(len(t), dtype=bool)
    if t0 is not None:
        m &= t >= t0
    if t1 is not None:
        m &= t <= t1
    return m


def acc_angles(acc) -> tuple[np.ndarray, np.ndarray]:
    """Pitch and roll (deg) from gravity alone, motion.c's ACC formula, for device-axis samples (N, 3) in g."""
    a = np.asarray(acc, dtype=np.float64).reshape(-1, 3)
    ax, ay, az = a[:, 0], a[:, 1], a[:, 2]
    pitch = np.degrees(np.arctan2(-ay, np.sqrt(ax * ax + az * az)))   # nose (+y) down: gravity leans to -y
    roll = np.degrees(np.arctan2(-ax, np.sqrt(ay * ay + az * az)))
    return pitch, roll


def _cents(s: np.ndarray) -> np.ndarray:
    a = np.abs(np.asarray(s, dtype=np.float64))
    a = a[a > 1e-3]
    if len(a) == 0:
        return a
    return 1200.0 * np.log2(a / np.median(a))


def _p2p(x) -> float | None:
    x = np.asarray(x, dtype=np.float64)
    x = x[np.isfinite(x)]
    return float(np.percentile(x, 99) - np.percentile(x, 1)) if len(x) > 2 else None


# ---------------------------------------------------------------- the session
@dataclass
class Session:
    path: Path
    meta: dict
    motion: dict                        # t, ax, ay, az, gx, gy, gz, flags (device axes, g, deg/s, nothing removed)
    raw: np.ndarray | None              # (N, 6) int, the chip's LSB, the same rows as motion
    state: dict                         # t and every state.csv column
    events: list = field(default_factory=list)
    screen_t: np.ndarray | None = None
    screen: np.ndarray | None = None    # (K, 1024) uint8, page format

    @property
    def name(self) -> str:
        return self.meta.get('name') or self.path.name

    @property
    def acc(self) -> np.ndarray:
        return np.column_stack([self.motion[k] for k in ('ax', 'ay', 'az')]) if len(self.motion.get('t', ())) else np.zeros((0, 3))

    @property
    def gyro(self) -> np.ndarray:
        return np.column_stack([self.motion[k] for k in ('gx', 'gy', 'gz')]) if len(self.motion.get('t', ())) else np.zeros((0, 3))

    def tune(self, name: str, default=None):
        t = self.meta.get('tune') or {}
        if name in t and t[name] is not None:
            v = t[name]
            return v.get('value', v.get('v')) if isinstance(v, dict) else v
        d = tune_default(name)
        return default if d is None else d

    @property
    def axis_map(self) -> tuple[int, int, int]:
        m = (self.meta.get('imu') or {}).get('axis_map')
        if m and len(m) == 3:
            return tuple(int(e) for e in m)
        return tuple(int(self.tune(k)) for k in ('motion.ax_x', 'motion.ax_y', 'motion.ax_z'))

    @property
    def rate_hz(self) -> float:
        r = (self.meta.get('imu') or {}).get('rate_hz')
        if r:
            return float(r)
        t = self.motion.get('t', np.zeros(0))
        if len(t) > 2:
            return float(round(1.0 / np.median(np.diff(t))))
        return float(synth.RATES[int(self.tune('imu.rate', 2)) & 3])

    @property
    def lsb(self) -> tuple[float, float]:
        imu = self.meta.get('imu') or {}
        ar = imu.get('acc_range', self.tune('imu.acc_range', 0))
        gr = imu.get('gyro_range', self.tune('imu.gyro_range', 2))
        if imu.get('acc_range_g') in (2, 4, 8, 16):                # the daemon's status names the ranges by size
            ar = (2, 4, 8, 16).index(imu['acc_range_g'])
        if imu.get('gyro_range_dps') in (250, 500, 1000, 2000):
            gr = (250, 500, 1000, 2000).index(imu['gyro_range_dps'])
        al = imu.get('acc_lsb') or synth.ACC_LSB[int(ar) & 3]
        gl = imu.get('gyro_lsb') or synth.GYRO_LSB[int(gr) & 3]
        return float(al), float(gl)

    def col(self, name: str):
        """A state column by name, an alias (speed, u) or a flag bit (rest, frozen, ...); None if the session has none."""
        s = self.state
        if name in s:
            return s[name]
        alias = {'speed': 'speed_cmd', 'u': 'tilt_u'}
        if name in alias and alias[name] in s:
            return s[alias[name]]
        if name in FLAG_BITS and 'flags' in s:
            return (np.asarray(s['flags'], dtype=np.int64) & FLAG_BITS[name]) != 0
        return None

    def chip_acc(self) -> np.ndarray:
        """The accelerometer in chip axes, g: raw.txt when it is there, else motion.csv taken back through the map."""
        if self.raw is not None and len(self.raw) == len(self.motion.get('t', ())):
            return self.raw[:, :3] / self.lsb[0]
        return synth.unmap(self.acc, self.axis_map)

    def chip_gyro(self) -> np.ndarray:
        if self.raw is not None and len(self.raw) == len(self.motion.get('t', ())):
            return self.raw[:, 3:] / self.lsb[1]
        return synth.unmap(self.gyro, self.axis_map)

    def steps(self) -> list[dict]:
        """The guided test's steps (meta test.steps): label, t0, t1, prompt."""
        test = self.meta.get('test') or {}
        return [dict(s) for s in test.get('steps') or []]

    def acc_offset_dev(self) -> np.ndarray:
        """The tune's accelerometer offsets (chip axes) in device axes."""
        off = np.array([float(self.tune(f'imu.acc_off_{a}', 0) or 0) for a in AXES])
        return synth.remap(off, self.axis_map)


def _read_csv(path: Path) -> dict:
    """A CSV with a header into {column: array}; numbers stay numbers, anything else stays text."""
    if not path.exists() or path.stat().st_size == 0:
        return {}
    with open(path) as f:
        header = f.readline().strip().split(',')
    try:
        data = np.loadtxt(path, delimiter=',', skiprows=1, ndmin=2)
        if data.size == 0:
            return {h: np.zeros(0) for h in header}
        return {h: data[:, i] for i, h in enumerate(header)}
    except ValueError:
        rows = list(csv.reader(open(path)))[1:]
        out = {}
        for i, h in enumerate(header):
            vals = [r[i] if i < len(r) else '' for r in rows]
            try:
                out[h] = np.array([float(v) if v != '' else np.nan for v in vals])
            except ValueError:
                out[h] = np.array(vals)
        return out


def load_session(path) -> Session:
    """A session folder into numpy: motion.csv, raw.txt, state.csv, events.jsonl, meta.json, screen.bin (each may be
    missing; what is missing comes back empty)."""
    p = Path(path)
    if not p.is_dir():
        raise FileNotFoundError(f'no session folder {p}')
    meta = {}
    if (p / 'meta.json').exists():
        meta = json.loads((p / 'meta.json').read_text() or '{}')
    motion = _read_csv(p / 'motion.csv')
    if motion:
        for k in ('t', 'ax', 'ay', 'az', 'gx', 'gy', 'gz'):
            motion.setdefault(k, np.zeros(len(next(iter(motion.values())))))
        motion['flags'] = np.asarray(motion.get('flags', np.zeros(len(motion['t']))), dtype=np.int64)
    else:
        motion = {k: np.zeros(0) for k in ('t', 'ax', 'ay', 'az', 'gx', 'gy', 'gz')}
        motion['flags'] = np.zeros(0, dtype=np.int64)
    raw = None
    if (p / 'raw.txt').exists() and (p / 'raw.txt').stat().st_size:
        raw = np.loadtxt(p / 'raw.txt', ndmin=2).astype(np.int64)
        if raw.shape[1] != 6:
            raw = None
    state = _read_csv(p / 'state.csv')
    events = []
    if (p / 'events.jsonl').exists():
        for line in (p / 'events.jsonl').read_text().splitlines():
            line = line.strip()
            if line:
                try:
                    events.append(json.loads(line))
                except ValueError:
                    pass
    events.sort(key=lambda e: e.get('t', 0))
    st, scr = None, None
    if (p / 'screen.bin').exists():
        b = (p / 'screen.bin').read_bytes()
        k = len(b) // 1032
        if k:
            rec = np.frombuffer(b[:k * 1032], dtype=np.uint8).reshape(k, 1032)
            st = np.array([struct.unpack('<d', bytes(r[:8]))[0] for r in rec])
            scr = rec[:, 8:].copy()
    return Session(p, meta, motion, raw, state, events, st, scr)


# ---------------------------------------------------------------- the stream itself
def stream_quality(sess: Session) -> dict:
    """Sample rates, gaps, FIFO overflows, injected samples, raw saturation and I2C failures."""
    t = sess.motion['t']
    fl = sess.motion['flags']
    m, f = {}, []
    if len(t) > 2:
        d = np.diff(t)
        rate = sess.rate_hz
        m['motion_hz'] = _f(1.0 / np.median(d), 1)
        gaps = int(np.sum(d > 1.5 / rate))
        m['motion_gaps'] = gaps
        if gaps:
            f.append(_finding('motion gaps', gaps, f'{gaps} gaps in the motion stream, the longest {d.max() * 1000:.0f} ms.'))
    m['overflow'] = int(np.sum((fl & 2) != 0))
    m['lost'] = int(np.sum((fl & 8) != 0))
    m['injected'] = int(np.sum((fl & 1) != 0))
    if m['overflow']:
        f.append(_finding('FIFO overflow', m['overflow'], 'The sensor\'s FIFO overflowed: samples are missing before these.'))
    if m['injected']:
        f.append(_finding('injected', m['injected'], 'Some motion was injected by the rig, not sensed.'))
    st = sess.state.get('t')
    if st is not None and len(st) > 2:
        m['state_hz'] = _f(1.0 / np.median(np.diff(st)), 1)
    sat = saturation(sess)
    m['saturated'] = sat['total']
    if sat['total']:
        f.append(_finding('saturation', sat['by_axis'], sat['note']))
    i2c = sess.col('i2c_fail')
    if i2c is not None and len(i2c):
        m['i2c_fail'] = int(np.nanmax(i2c) - np.nanmin(i2c))
        if m['i2c_fail']:
            f.append(_finding('I2C failures', m['i2c_fail'], 'The IMU did not answer this many times during the session.'))
    return _result(m, f)


def saturation(sess: Session, t0=None, t1=None) -> dict:
    """Raw samples at the rail (+32767 or -32768), per chip channel."""
    names = ('acc x', 'acc y', 'acc z', 'gyro x', 'gyro y', 'gyro z')
    if sess.raw is None:
        return {'total': 0, 'by_axis': {}, 'note': 'no raw.txt: saturation not checked'}
    r = sess.raw
    if len(r) == len(sess.motion['t']):
        r = r[_win(sess.motion['t'], t0, t1)]
    hit = (r >= 32767) | (r <= -32768)
    by = {n: int(c) for n, c in zip(names, hit.sum(axis=0)) if c}
    al, gl = sess.lsb
    note = ''
    if by:
        parts = [f'{n} {c}' for n, c in by.items()]
        note = (f'Samples at the rail (chip axes): {", ".join(parts)}. The range is {32768 / al:.0f} g and '
                f'{32768 / gl:.0f} deg/s: what went past it is lost.')
    return {'total': int(hit.sum()), 'by_axis': by, 'note': note}


# ---------------------------------------------------------------- lying still
def allan(x, rate_hz: float, taus=(0.1, 1.0, 10.0)) -> dict:
    """Overlapping Allan deviation of a rate signal (deg/s) at each tau (s); None where the data is shorter than 4 tau."""
    x = np.asarray(x, dtype=np.float64)
    theta = np.concatenate([[0.0], np.cumsum(x)]) / rate_hz
    out = {}
    for tau in taus:
        m = int(round(tau * rate_hz))
        if m < 1 or len(x) < 4 * m:
            out[tau] = None
            continue
        d = theta[2 * m:] - 2 * theta[m:-m] + theta[:-2 * m]
        out[tau] = float(np.sqrt(np.mean(d * d) / (2 * tau * tau)))
    return out


def rest_stats(sess: Session, t0=None, t1=None, flat: bool = False) -> dict:
    """The board lying still: per-axis mean and noise of the accelerometer and gyro, the tilt noise of gravity alone,
    the gyro's bias (device and chip axes), the accelerometer's offset if it lies flat, Allan deviation, saturation.
    flat=True says the board lay flat, screen up, and lets the offsets be suggested."""
    m_ = _win(sess.motion['t'], t0, t1)
    acc, gyro = sess.acc[m_], sess.gyro[m_]
    n = len(acc)
    if n < 20:
        return _result({'n': n}, [_finding('samples', n, 'Too few motion samples in this window to say anything.')])
    rate = sess.rate_hz
    secs = n / rate
    amap = sess.axis_map
    an = np.linalg.norm(acc, axis=1)
    am, asd = acc.mean(0), acc.std(0)
    gm, gsd = gyro.mean(0), gyro.std(0)
    pitch, roll = acc_angles(acc - sess.acc_offset_dev())
    still = bool(gsd.max() < 1.0 and an.std() < 0.02)
    gm_chip = synth.unmap(gm, amap)
    metrics = {
        'secs': _f(secs, 2), 'n': n, 'still': still,
        'acc_mean': _v(am, 4), 'acc_std': _v(asd, 4), 'acc_norm_mean': _f(an.mean(), 4), 'acc_norm_std': _f(an.std(), 4),
        'gyro_mean': _v(gm, 3), 'gyro_std': _v(gsd, 3),
        'gyro_bias': _v(gm, 3), 'gyro_bias_chip': _v(gm_chip, 3),
        'pitch_acc_mean': _f(pitch.mean(), 2), 'roll_acc_mean': _f(roll.mean(), 2),
        'pitch_acc_noise': _f(pitch.std(), 3), 'roll_acc_noise': _f(roll.std(), 3),
    }
    f, sug = [], {}
    f.append(_finding('gyro bias', metrics['gyro_bias'],
                      'deg/s, device x y z (chip x y z: ' + ', '.join(f'{v:+.2f}' for v in gm_chip) +
                      f'). Measured over {secs:.1f} s.'))
    f.append(_finding('gyro noise', metrics['gyro_std'], 'deg/s rms per sample, device x y z.'))
    f.append(_finding('tilt noise', [metrics['pitch_acc_noise'], metrics['roll_acc_noise']],
                      'deg rms of pitch and roll from gravity alone, per sample, before any smoothing.'))
    bp, br = sess.col('pitch'), sess.col('roll')
    if bp is not None and len(bp):
        sm = _win(sess.state['t'], t0 if t0 is not None else sess.motion['t'][m_][0],
                  t1 if t1 is not None else sess.motion['t'][m_][-1])
        if sm.sum() > 3:
            metrics['pitch_board_std'] = _f(np.std(bp[sm]), 3)
            metrics['roll_board_std'] = _f(np.std(br[sm]), 3)
            f.append(_finding('board angle noise', [metrics['pitch_board_std'], metrics['roll_board_std']],
                              'deg rms of the board\'s own pitch and roll (what the controls use).'))
        bx = sess.col('bias_x')
        if bx is not None and sm.sum():
            learned = np.array([sess.col(f'bias_{a}')[sm][-1] for a in AXES])
            fixed = np.array([float(sess.tune(f'imu.gyro_bias_{a}', 0) or 0) for a in AXES])
            metrics['bias_learned_chip'] = _v(learned, 3)
            err = gm_chip - fixed - learned
            metrics['bias_left_chip'] = _v(err, 3)
            if still and secs > 3:
                f.append(_finding('learned bias', metrics['bias_learned_chip'],
                                  'deg/s, chip axes, what the board had learned at the end; still left over: ' +
                                  ', '.join(f'{v:+.2f}' for v in err) + ' deg/s.'))
    ad = {a: allan(gyro[:, i], rate) for i, a in enumerate(AXES)}
    metrics['allan'] = {a: {f'{tau:g}s': _f(v, 4) for tau, v in d.items()} for a, d in ad.items()}
    ok = [tau for tau in (0.1, 1.0, 10.0) if ad['x'][tau] is not None]
    f.append(_finding('Allan deviation', metrics['allan'],
                      f'deg/s at tau {", ".join(f"{t:g} s" for t in ok) or "(none)"}'
                      + ('; 10 s needs at least 40 s still.' if 10.0 not in ok else '.')))
    sat = saturation(sess, t0, t1)
    metrics['saturated'] = sat['total']
    if sat['total']:
        f.append(_finding('saturation', sat['by_axis'], sat['note']))

    # the accelerometer's offset, if this was flat on a table
    flat_now = abs(am[2]) > 0.9 and abs(am[0]) < 0.1 and abs(am[1]) < 0.1
    if flat_now:
        up = np.array([0, 0, 1.0 if am[2] > 0 else -1.0])
        off_dev = am - up
        off_chip = synth.unmap(off_dev, amap)
        metrics['acc_offset'] = _v(off_dev, 4)
        metrics['acc_offset_chip'] = _v(off_chip, 4)
        f.append(_finding('accelerometer offset', metrics['acc_offset_chip'],
                          'g, chip axes, assuming the board lay flat' + (' screen down' if up[2] < 0 else '') +
                          f' (device axes {", ".join(f"{v:+.4f}" for v in off_dev)}; the chip axes need the axis map, '
                          f'{"/".join(CODES[e] for e in amap)}). x and y include the table\'s own slope.'))
    else:
        f.append(_finding('accelerometer offset', None, 'Not flat (gravity is not on z): no offset estimate.'))

    if not still:
        f.append(_finding('still', False, 'The board moved during this window (gyro or |a| noisy): bias and offset '
                                          'are not trusted, nothing suggested.'))
    elif secs >= 3:
        cur = np.array([float(sess.tune(f'imu.gyro_bias_{a}', 0) or 0) for a in AXES])
        if np.max(np.abs(gm_chip - cur)) > 0.05:
            for a, v in zip(AXES, gm_chip):
                sug[f'imu.gyro_bias_{a}'] = _snap(f'imu.gyro_bias_{a}', v)
            note = 'Store the measured gyro bias (chip axes).'
            if int(sess.tune('imu.auto_bias', 1)):
                note += ' imu.auto_bias also learns it after the board lies still; a stored bias starts it closer.'
            f.append(_finding('suggest gyro bias', [sug[f'imu.gyro_bias_{a}'] for a in AXES], note))
        if flat and flat_now:
            cur = np.array([float(sess.tune(f'imu.acc_off_{a}', 0) or 0) for a in AXES])
            if np.max(np.abs(off_chip - cur)) > 0.003:
                for a, v in zip(AXES, off_chip):
                    sug[f'imu.acc_off_{a}'] = _snap(f'imu.acc_off_{a}', v)
                f.append(_finding('suggest accelerometer offset', [sug[f'imu.acc_off_{a}'] for a in AXES],
                                  'Chip axes. Flat on this table then reads 0 deg; a sloping table moves x and y.'))
    return _result(metrics, f, sug)


def drift(sess: Session, t0=None, t1=None, still_only: bool = True) -> dict:
    """How the board's yaw, pitch and roll creep (deg/min) while it lies still, and what the raw gyro alone would do."""
    st = sess.state
    if 't' not in st or len(st['t']) < 5:
        return _result({}, [_finding('state', None, 'No state.csv rows: the board\'s angles were not recorded.')])
    m = _win(st['t'], t0, t1)
    rest = sess.col('rest')
    used_rest = False
    if still_only and rest is not None and (m & rest).sum() > 10:
        m = m & rest
        used_rest = True
    t = st['t'][m]
    metrics, f = {'secs': _f(t[-1] - t[0] if len(t) > 1 else 0, 2), 'rest_only': used_rest}, []
    if len(t) < 5 or t[-1] - t[0] < 1:
        return _result(metrics, [_finding('drift', None, 'Less than a second of data: no drift.')])
    for name in ('yaw', 'pitch', 'roll'):
        x = sess.col(name)
        if x is None:
            continue
        x = np.degrees(np.unwrap(np.radians(x[m])))
        slope = np.polyfit(t - t[0], x, 1)[0] * 60.0
        metrics[f'{name}_drift'] = _f(slope, 3)
    mm = _win(sess.motion['t'], t[0], t[-1])
    if mm.sum() > 10:
        g = sess.gyro[mm]
        metrics['raw_gyro_drift'] = _v(g.mean(0) * 60.0, 2)
    f.append(_finding('yaw drift', metrics.get('yaw_drift'),
                      'deg/min of the board\'s yaw' + (' while it thinks it is still' if used_rest else '') +
                      '. Yaw has no compass: it drifts by design; motion.yaw_reset zeroes it.'))
    if 'raw_gyro_drift' in metrics:
        f.append(_finding('raw gyro drift', metrics['raw_gyro_drift'],
                          'deg/min, device x y z: what the gyro alone would drift with no bias removed.'))
    for name in ('pitch', 'roll'):
        v = metrics.get(f'{name}_drift')
        if v is not None:
            f.append(_finding(f'{name} drift', v, f'deg/min of the board\'s {name} while still'
                              + (' (gravity holds it: should be near 0).' if abs(v) < 0.5 else ': it creeps.')))
    return _result(metrics, f)


# ---------------------------------------------------------------- axes
EXPECT_UP = {                            # what the accelerometer reads in each labelled pose, device axes
    'flat': (0, 0, 1), 'screen_down': (0, 0, -1),
    'nose_down': (0, -1, 1), 'nose_up': (0, 1, 1),
    'right_down': (-1, 0, 1), 'left_down': (1, 0, 1),
    'facing': (0, 1, 0), 'standing': (0, 1, 0), 'upside': (0, -1, 0),
    'right_edge': (-1, 0, 0), 'left_edge': (1, 0, 0),
}


def _expected_up(label: str, angle=None) -> np.ndarray | None:
    key = label.lower().replace(' ', '_').replace('-', '_')
    for k in EXPECT_UP:
        if key.startswith(k):
            v = np.array(EXPECT_UP[k], dtype=np.float64)
            if k in ('nose_down', 'nose_up', 'right_down', 'left_down'):
                a = math.radians(60.0 if angle is None else float(angle))
                v = np.array([v[0] * math.sin(a), v[1] * math.sin(a), math.cos(a)])
            return v / np.linalg.norm(v)
    return None


def _rotations() -> list[tuple[int, int, int]]:
    """The 24 axis maps a real mounting can give (proper rotations: no mirror images)."""
    out = []
    for c in range(216):
        m = (c // 36, (c // 6) % 6, c % 6)
        if len({e >> 1 for e in m}) != 3:
            continue
        M = np.zeros((3, 3))
        for k, e in enumerate(m):
            M[k, e >> 1] = -1 if e & 1 else 1
        if np.linalg.det(M) > 0:
            out.append(m)
    return out


def _segs(segments) -> list[dict]:
    out = []
    for s in segments:
        if isinstance(s, dict):
            out.append(dict(s))
        else:
            out.append({'label': s[0], 't0': s[1], 't1': s[2], **({'angle': s[3]} if len(s) > 3 else {})})
    return out


def axis_check(sess: Session, segments) -> dict:
    """Which chip axis lies behind each device axis, from labelled holds (flat, nose_down, right_down, facing, ...):
    segments are dicts {label, t0, t1[, angle]} or tuples (label, t0, t1). Works from raw.txt (the chip's own axes),
    so a wrong map in the session does not matter. Axes no hold leaned on stay unknown; a real mounting is a rotation,
    so two known axes fix the third."""
    segs = _segs(segments)
    chip = sess.chip_acc()
    t = sess.motion['t']
    rows, f = [], []
    for s in segs:
        e = _expected_up(s['label'], s.get('angle'))
        m = _win(t, s.get('t0'), s.get('t1'))
        if e is None or m.sum() < 10:
            f.append(_finding('hold', s['label'], 'Unknown label or too few samples: skipped.' if e is None else
                              'Too few samples in this hold: skipped.'))
            continue
        g = chip[m].mean(0)
        sd = float(np.linalg.norm(chip[m].std(0)))
        rows.append({'label': s['label'], 'expect': e, 'chip': g, 'norm': float(np.linalg.norm(g)), 'std': sd})
    current = sess.axis_map
    metrics = {'current': list(current), 'current_codes': [CODES[e] for e in current]}
    if not rows:
        return _result(metrics, f + [_finding('axes', None, 'No usable holds: nothing checked.')], {},
                       axis_map=None, determined=[False] * 3, current_ok=None)
    E = np.array([r['expect'] for r in rows])
    G = np.array([r['chip'] / max(r['norm'], 1e-6) for r in rows])
    weight = (E * E).sum(0)
    det = weight >= 0.3
    det_axes = int(det.sum())
    rots = _rotations()

    def resid(mp):
        return float(((E - synth.remap(G, mp)) ** 2).sum())
    scores = sorted((resid(mp), mp) for mp in rots)
    best = scores[0][1]
    if det_axes == 2:                    # the third follows from the two (a rotation), so all three are known
        det = np.ones(3, dtype=bool)
    # among maps as good on the known axes, keep the current one's choices for the unknown axes
    cands = [mp for _, mp in scores if all(mp[k] == best[k] for k in range(3) if det[k])]
    pick = max(cands, key=lambda mp: (sum(mp[k] == current[k] for k in range(3)), -resid(mp)))
    ok_rows = [k for k in range(3) if det[k]]
    current_ok = all(pick[k] == current[k] for k in ok_rows)
    cur_valid = len({e >> 1 for e in current}) == 3 and tuple(current) in rots
    for r in rows:
        dev = synth.remap(r['chip'] / max(r['norm'], 1e-6), pick)
        r['err_deg'] = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(dev, r['expect']))))))
    metrics.update({
        'suggested': list(pick), 'suggested_codes': [CODES[e] for e in pick], 'determined': [bool(x) for x in det],
        'current_ok': current_ok, 'residual': _f(scores[0][0], 4),
        'holds': [{'label': r['label'], 'chip_g': _v(r['chip'], 3), '|a|': _f(r['norm'], 3), 'std_g': _f(r['std'], 4),
                   'off_deg': _f(r['err_deg'], 1)} for r in rows]})
    for r in rows:
        if r['err_deg'] > 35:
            f.append(_finding('hold', r['label'], f'This hold is {r["err_deg"]:.0f} deg away from what "{r["label"]}" '
                                                  'means: was it held as asked?'))
        if abs(r['norm'] - 1) > 0.1 or r['std'] > 0.05:
            f.append(_finding('hold', r['label'], f'|a| {r["norm"]:.2f} g, spread {r["std"]:.3f} g: the board moved '
                                                  'during this hold.'))
    names = ['device ' + a for a in AXES]
    for k in range(3):
        if not det[k]:
            f.append(_finding(names[k], None, 'Not checked: no hold leaned on this axis (add nose_down for y, '
                                              'right_down for x).'))
        elif pick[k] == current[k]:
            f.append(_finding(names[k], CODES[pick[k]], f'Right: device {AXES[k]} is chip {CODES[pick[k]]}.'))
        else:
            f.append(_finding(names[k], CODES[pick[k]], f'Wrong: device {AXES[k]} is chip {CODES[pick[k]]}, the map '
                                                        f'says {CODES[current[k]]}.'))
    if not cur_valid:
        f.append(_finding('current map', metrics['current_codes'], 'The current map is not a rotation (a chip axis '
                                                                   'twice, or a mirror image): no real mounting gives it.'))
    f.append(_finding('gyro', None, 'The gyro sits on the same chip axes, so the same map serves it.'))
    sug = {}
    if not current_ok and det_axes >= 2:
        sug = {f'motion.ax_{a}': int(pick[k]) for k, a in enumerate(AXES)}
    elif not current_ok:
        f.append(_finding('suggest', None, 'Only one axis was checked: lay it on its edges too before changing the map.'))
    return _result(metrics, f, sug, axis_map=list(pick), determined=[bool(x) for x in det], current_ok=current_ok)


# ---------------------------------------------------------------- holds and responses
def _settle(t: np.ndarray, x: np.ndarray, tol: float) -> tuple[float | None, float]:
    """When x settles within tol of its final value (the median of the last 40 %): (seconds from t[0], final)."""
    if len(x) < 4:
        return None, float(np.median(x)) if len(x) else float('nan')
    final = float(np.median(x[int(len(x) * 0.6):]))
    out = np.abs(x - final) > tol
    if not out.any():
        return 0.0, final
    last = int(np.nonzero(out)[0][-1])
    if last >= len(x) - 1:
        return None, final
    return float(t[last + 1] - t[0]), final


def holds(sess: Session, steps) -> dict:
    """Each hold (dicts {label, t0, t1[, pitch, roll]}): the board's pitch and roll against gravity alone, their noise,
    the speed asked, the zone, and how long the board took to settle."""
    out, f = [], []
    st = sess.state
    have_state = 't' in st and len(st['t']) > 0
    off = sess.acc_offset_dev()
    for s in _segs(steps):
        t0, t1 = s.get('t0'), s.get('t1')
        row = {'label': s.get('label', '')}
        for k in ('pitch', 'roll'):
            if s.get(k) is not None:
                row[f'target_{k}'] = _f(s[k], 2)
        settle_from = t0
        if have_state:
            m = _win(st['t'], t0, t1)
            if m.sum() >= 3:
                ts, bp, br = st['t'][m], sess.col('pitch')[m], sess.col('roll')[m]
                tol = max(1.0, 3.0 * float(np.std(bp[int(len(bp) * 0.6):])))
                sp, _ = _settle(ts, bp, tol)
                sr, _ = _settle(ts, br, max(1.0, 3.0 * float(np.std(br[int(len(br) * 0.6):]))))
                settle = None if sp is None or sr is None else max(sp, sr)
                row['settle_s'] = _f(settle, 3)
                cut = ts[0] + (settle if settle is not None else (ts[-1] - ts[0]) * 0.6)
                settle_from = cut
                k = ts >= cut
                row.update({'pitch': _f(bp[k].mean(), 2), 'pitch_std': _f(bp[k].std(), 3),
                            'roll': _f(br[k].mean(), 2), 'roll_std': _f(br[k].std(), 3)})
                sc = sess.col('speed_cmd')
                if sc is not None:
                    row.update({'speed': _f(sc[m][k].mean(), 4), 'speed_std': _f(sc[m][k].std(), 5)})
                    c = _cents(sc[m][k])
                    row['speed_cents_std'] = _f(c.std(), 2) if len(c) > 2 else None
                z = sess.col('zone')
                if z is not None:
                    zz = np.round(z[m][k]).astype(int)
                    zone = int(np.bincount(np.clip(zz, 0, 2)).argmax()) if len(zz) else 0
                    det = sess.col('detent')
                    row['zone'] = 'detent' if det is not None and np.median(det[m][k]) > 0 else ZONES[zone]
        mm = _win(sess.motion['t'], settle_from, t1)
        if mm.sum() >= 3:
            ap, ar = acc_angles(sess.acc[mm] - off)
            row.update({'pitch_acc': _f(ap.mean(), 2), 'pitch_acc_std': _f(ap.std(), 3),
                        'roll_acc': _f(ar.mean(), 2), 'roll_acc_std': _f(ar.std(), 3)})
            if row.get('pitch') is not None:
                row['pitch_vs_acc'] = _f(row['pitch'] - row['pitch_acc'], 2)
                row['roll_vs_acc'] = _f(row['roll'] - row['roll_acc'], 2)
        for k in ('pitch', 'roll'):
            if f'target_{k}' in row and row.get(k) is not None:
                row[f'{k}_err'] = _f(row[k] - row[f'target_{k}'], 2)
        out.append(row)
    for r in out:
        bits = []
        if r.get('pitch') is not None:
            bits.append(f'pitch {r["pitch"]:+.1f} (+/-{r["pitch_std"]:.2f}), roll {r["roll"]:+.1f}')
        if r.get('pitch_acc') is not None:
            bits.append(f'gravity alone {r["pitch_acc"]:+.1f}')
        if r.get('speed') is not None:
            bits.append(f'speed {r["speed"]:.3f}x')
        if r.get('settle_s') is not None:
            bits.append(f'settled in {r["settle_s"] * 1000:.0f} ms')
        f.append(_finding(r['label'], r.get('pitch'), ', '.join(bits) + '.'))
    pv = [r['pitch_std'] for r in out if r.get('pitch_std') is not None]
    metrics = {'steps': out, 'pitch_std_max': _f(max(pv), 3) if pv else None,
               'pitch_err_max': _f(max((abs(r['pitch_err']) for r in out if r.get('pitch_err') is not None),
                                       default=None) if any(r.get('pitch_err') is not None for r in out) else None, 2)}
    return _result(metrics, f)


def step_response(t, x, t_step: float, target: float, baseline: float | None = None, band: float = 0.05,
                  t_end: float | None = None) -> dict:
    """A step from the baseline (the mean of the 0.2 s before t_step) to target, up to t_end (the next step): delay to
    10, 63 and 90 % (ms), overshoot (% of the step) and settling time into +/- band of the step (ms; None if it never
    settles)."""
    t, x = np.asarray(t, dtype=np.float64), np.asarray(x, dtype=np.float64)
    pre = (t < t_step) & (t >= t_step - 0.2)
    if baseline is None:
        baseline = float(x[pre].mean()) if pre.any() else float(x[t >= t_step][0])
    d = target - baseline
    if abs(d) < 1e-9:
        return _result({}, [_finding('step', 0, 'The target equals the start: no step to measure.')])
    k = (t >= t_step) & (t <= (t_end if t_end is not None else np.inf))
    if k.sum() < 2:
        return _result({}, [_finding('step', None, 'Too few samples after the step.')])
    tt, y = t[k], (x[k] - baseline) / d

    def cross(level):
        i = np.nonzero(y >= level)[0]
        if not len(i):
            return None
        i = int(i[0])
        if i == 0:
            return float(tt[0] - t_step)
        y0, y1 = y[i - 1], y[i]
        return float(tt[i - 1] + (level - y0) / (y1 - y0) * (tt[i] - tt[i - 1]) - t_step)
    over = max(0.0, float(y.max() - 1.0)) * 100.0
    outside = np.nonzero(np.abs(y - 1.0) > band)[0]
    if not len(outside):
        settle = 0.0
    elif outside[-1] >= len(y) - 1:
        settle = None
    else:
        settle = float(tt[outside[-1] + 1] - t_step)
    ms = lambda v: None if v is None else _f(v * 1000.0, 1)
    metrics = {'baseline': _f(baseline, 4), 'target': _f(target, 4), 'delay_10_ms': ms(cross(0.1)),
               'delay_63_ms': ms(cross(0.632)), 'delay_90_ms': ms(cross(0.9)), 'overshoot_pct': _f(over, 1),
               'settle_ms': ms(settle)}
    f = [_finding('response', metrics['delay_63_ms'],
                  f'63 % of the step after {metrics["delay_63_ms"]} ms, 90 % after {metrics["delay_90_ms"]} ms, '
                  f'overshoot {metrics["overshoot_pct"]} %, settled ' +
                  ('never' if settle is None else f'after {metrics["settle_ms"]} ms') + '.')]
    return _result(metrics, f)


def jitter(sess: Session, t0=None, t1=None) -> dict:
    """While held: how much the speed asked shakes (std, and its flutter in cents), and the angles' noise."""
    st = sess.state
    if 't' not in st:
        return _result({}, [_finding('state', None, 'No state.csv: nothing to measure.')])
    m = _win(st['t'], t0, t1)
    metrics, f = {'n': int(m.sum())}, []
    for k in ('pitch', 'roll'):
        x = sess.col(k)
        if x is not None and m.sum() > 2:
            metrics[f'{k}_std'] = _f(x[m].std(), 3)
            metrics[f'{k}_p2p'] = _f(_p2p(x[m]), 3)
    for k in ('speed_cmd', 'speed_now'):
        s = sess.col(k)
        if s is None or m.sum() < 3:
            continue
        s = s[m]
        metrics[f'{k}_mean'] = _f(s.mean(), 4)
        metrics[f'{k}_std'] = _f(s.std(), 5)
        c = _cents(s)
        if len(c) > 2:
            metrics[f'{k}_cents_std'] = _f(c.std(), 2)
            metrics[f'{k}_cents_p2p'] = _f(_p2p(c), 2)
    if metrics.get('speed_cmd_std') == 0:
        f.append(_finding('flutter', 0, 'The speed did not move at all: in the dead zone it sits at Speed min. Hold '
                                        'it outside the dead zone to measure flutter.'))
    elif metrics.get('speed_cmd_cents_std') is not None:
        c = metrics['speed_cmd_cents_std']
        f.append(_finding('flutter', c, f'{c:.1f} cents rms on the speed asked (p2p {metrics["speed_cmd_cents_p2p"]:.1f}): '
                          + ('below what an ear hears on most material.' if c < 3 else
                             'audible as a wobble on held notes.' if c > 8 else 'at the edge of hearing on pure tones.')))
    if metrics.get('pitch_std') is not None:
        f.append(_finding('angle noise', [metrics['pitch_std'], metrics.get('roll_std')], 'deg rms, pitch and roll.'))
    return _result(metrics, f)


def shake_rejection(sess: Session, shake, rest) -> dict:
    """A shake without tilting (shake = (t0, t1)) against rest (t0, t1): how far the pitch and the speed moved, how
    hard it shook (|a| off 1 g), and how much turning the gyro saw. Linear shaking that moves the angle is gravity
    misread: the case for gyro fusion (motion.fusion COMP) over ACC."""
    out = {}
    for name, (t0, t1) in (('shake', shake), ('rest', rest)):
        mm = _win(sess.motion['t'], t0, t1)
        d = {}
        if mm.sum() > 5:
            an = np.linalg.norm(sess.acc[mm], axis=1)
            d['acc_dev_rms'] = float(np.sqrt(np.mean((an - 1.0) ** 2)))
            g = sess.gyro[mm]
            d['gyro'] = g
        sm = _win(sess.state['t'], t0, t1) if 't' in sess.state else np.zeros(0, dtype=bool)
        for k in ('pitch', 'roll', 'speed_cmd'):
            x = sess.col(k)
            if x is not None and sm.sum() > 2:
                d[f'{k}_p2p'] = _p2p(x[sm])
                d[f'{k}_std'] = float(x[sm].std())
                if k == 'speed_cmd':
                    c = _cents(x[sm])
                    d['speed_cents_p2p'] = _p2p(c) if len(c) > 2 else None
        out[name] = d
    rest_g = out['rest'].get('gyro')
    bias = rest_g.mean(0) if rest_g is not None and len(rest_g) else np.zeros(3)
    for name in ('shake', 'rest'):
        g = out[name].pop('gyro', None)
        if g is not None and len(g):
            g = g - bias
            out[name]['gyro_rms'] = float(np.sqrt(np.mean((g * g).sum(1))))
            ang = np.cumsum(g, axis=0) / sess.rate_hz        # what the gyro saw turn, deg
            out[name]['gyro_pitch_p2p'] = _p2p(ang[:, 0])
            out[name]['gyro_roll_p2p'] = _p2p(ang[:, 1])
    metrics = {n: {k: _f(v, 3) for k, v in d.items()} for n, d in out.items()}
    f, sug = [], {}
    s, r = out['shake'], out['rest']
    if 'acc_dev_rms' in s:
        f.append(_finding('shake strength', _f(s['acc_dev_rms'], 3), f'g rms off 1 g during the shake '
                                                                     f'(at rest {r.get("acc_dev_rms", 0):.3f}).'))
    sp, rp = s.get('pitch_p2p'), r.get('pitch_p2p')
    if sp is not None:
        f.append(_finding('pitch moved', _f(sp, 2), f'deg p2p during the shake, {rp or 0:.2f} at rest; '
                                                    f'roll {s.get("roll_p2p") or 0:.2f}.'))
        metrics['pitch_ratio'] = _f(sp / max(rp or 0, 0.05), 1)
    if s.get('speed_cents_p2p') is not None:
        f.append(_finding('speed moved', _f(s['speed_cents_p2p'], 1),
                          f'cents p2p during the shake ({r.get("speed_cents_p2p") or 0:.1f} at rest).'))
    seen = s.get('gyro_pitch_p2p')
    fusion = int(sess.tune('motion.fusion', 0))
    if sp is not None and seen is not None:
        misread = sp - seen
        metrics['misread_deg'] = _f(misread, 2)
        if sp > 2.0 and sp > 3 * (rp or 0) and seen < 0.3 * sp:
            note = (f'The pitch moved {sp:.1f} deg but the gyro saw only {seen:.1f} deg of turning: the rest is '
                    'shaking read as tilt.')
            if fusion == 0:
                note += (' ACC takes gravity alone; COMP lets the gyro hold the angle and gravity correct it slowly. '
                         'Replay this session with motion.fusion COMP to hear the difference before keeping it.')
                sug['motion.fusion'] = 1
            f.append(_finding('shake misread as tilt', _f(misread, 2), note))
        else:
            f.append(_finding('shake misread as tilt', _f(misread, 2), 'The angle held through the shake.'))
    return _result(metrics, f, sug)


# ---------------------------------------------------------------- the encoder, REC and the joystick
def _events_of(src):
    if isinstance(src, Session):
        return src.events, (lambda n: src.tune(n))
    return list(src or []), tune_default


def _in(t, w):
    return w is not None and w[0] <= t <= w[1]


def encoder_stats(events, expected: dict | None = None, tune: dict | None = None) -> dict:
    """The encoder's events (a Session or its events): turns by sign, taps, holds, long presses, bounces, double taps and
    press lengths. expected (optional) says what was asked: {"right": 10, "left": 10, "taps": 5, "holds": 3,
    "doubles": 2, "longs": 0, "windows": {"right": (t0, t1), ...}}; with it, the encoder's settings are checked and
    enc.dir / enc.steps / enc.tap_min / enc.hold / enc.double suggested where the counts say so."""
    evs, tdef = _events_of(events)
    tv = lambda n: float((tune or {}).get(n, tdef(n)))
    exp = dict(expected or {})
    win = exp.get('windows') or {}
    turns = [(float(e.get('t', 0)), float(e.get('v', 0))) for e in evs if e.get('name') == 'enc.turn']
    pos = sum(v for _, v in turns if v > 0)
    neg = sum(v for _, v in turns if v < 0)
    presses, cls, long_now = [], None, False
    for e in evs:
        n = e.get('name', '')
        if n == 'enc.down':
            long_now = False
        elif n == 'enc.long':
            long_now = True
        elif n == 'enc.up':
            presses.append({'t': float(e.get('t', 0)), 'ms': int(e.get('ms', 0)), 'cls': 'long' if long_now else None})
        elif n in ('enc.tap', 'enc.hold', 'enc.bounce') and presses and presses[-1]['cls'] is None:
            presses[-1]['cls'] = n[4:]
    for p in presses:                      # the events may be missing: the table's rule then
        if p['cls'] is None:
            p['cls'] = 'bounce' if p['ms'] < tv('enc.tap_min') else 'tap' if p['ms'] < tv('enc.hold') else 'hold'
    count = lambda name: sum(1 for e in evs if e.get('name') == name)
    taps = [p for p in presses if p['cls'] == 'tap']
    metrics = {
        'turn_events': len(turns), 'turned_pos': _f(pos, 0), 'turned_neg': _f(neg, 0), 'turned_net': _f(pos + neg, 0),
        'presses': len(presses), 'taps': count('enc.tap'), 'holds': count('enc.hold'), 'longs': count('enc.long'),
        'bounces': count('enc.bounce'), 'doubles': count('enc.double'),
        'press_ms': {c: sorted(p['ms'] for p in presses if p['cls'] == c) for c in ('bounce', 'tap', 'hold', 'long')},
    }
    tap_t = [p['t'] for p in taps]
    metrics['tap_gaps_ms'] = [int(round((b - a) * 1000)) for a, b in zip(tap_t, tap_t[1:])]
    f, sug = [], {}
    f.append(_finding('turns', [metrics['turned_pos'], metrics['turned_neg']],
                      f'{len(turns)} turn events: {pos:+.0f} detents one way, {neg:+.0f} the other.'))
    f.append(_finding('presses', len(presses), f'{metrics["taps"]} taps, {metrics["holds"]} holds, {metrics["longs"]} '
                                               f'long, {metrics["bounces"]} bounces, {metrics["doubles"]} double taps.'))
    tiny = [p for p in presses if p['ms'] < 10]
    if tiny:
        f.append(_finding('contact bounce', len(tiny), f'{len(tiny)} presses under 10 ms: the switch chatters; '
                                                       f'enc.tap_min {tv("enc.tap_min"):.0f} ms filters them.'))

    # direction and counts per detent
    R, L = exp.get('right'), exp.get('left')
    if R or L:
        if win.get('right') or win.get('left'):
            cr = sum(v for t, v in turns if _in(t, win.get('right'))) if R else None
            cl = sum(v for t, v in turns if _in(t, win.get('left'))) if L else None
        elif R and L:                      # right first, then left: split where the running count turns round
            cs = np.cumsum([v for _, v in turns]) if turns else np.zeros(1)
            i = int(np.argmax(np.abs(cs)))
            cr, cl = float(cs[i]), float(cs[-1] - cs[i])
        else:
            cr, cl = (pos + neg, None) if R else (None, pos + neg)
        metrics['counted_right'], metrics['counted_left'] = _f(cr, 0), _f(cl, 0)
        signs = [np.sign(c) * s for c, s in ((cr, 1), (cl, -1)) if c]
        asked, got = (R or 0) + (L or 0), abs(cr or 0) + abs(cl or 0)
        if signs and all(s < 0 for s in signs):
            what = f'asked for {R} right, counted {cr:+.0f}' if R else f'asked for {L} left, counted {cl:+.0f}'
            sug['enc.dir'] = 1 - int(tv('enc.dir'))
            f.append(_finding('direction', 'reversed', f'{what}: flip enc.dir (to {"+1" if sug["enc.dir"] else "-1"}).'))
        elif signs and all(s > 0 for s in signs):
            f.append(_finding('direction', 'right', 'Turns count the way they were turned.'))
        elif signs:
            f.append(_finding('direction', 'mixed', 'One way counted right and the other wrong: a skipping encoder or '
                                                    'the turns were not as asked. Nothing suggested.'))
        if asked and got:
            ratio = got / asked
            steps = tv('enc.steps')
            metrics['count_ratio'] = _f(ratio, 3)
            hw = steps * ratio
            new = int(round(hw))
            if abs(ratio - 1) > 0.25 and 1 <= new <= 8 and abs(hw - new) < 0.2 * new and new != steps:
                sug['enc.steps'] = new
                f.append(_finding('counts per detent', new, f'asked {asked} detents, counted {got:.0f}: enc.steps {new}.'))
            elif abs(ratio - 1) > 0.25:
                f.append(_finding('counts per detent', _f(ratio, 2), f'asked {asked} detents, counted {got:.0f}: no '
                                                                    'whole enc.steps fits; a skipping or noisy encoder?'))
            elif abs(got - asked) >= 1:
                f.append(_finding('counts per detent', _f(ratio, 2), f'asked {asked}, counted {got:.0f}: close enough '
                                                                    'to be the hand, not the setting.'))
            if tv('enc.accel') > 0:
                f.append(_finding('acceleration', tv('enc.accel'), 'enc.accel is on: fast turns count more. Turn '
                                                                   'slowly for this check.'))

    # taps, holds and double taps against what was asked
    T, H, D = int(exp.get('taps', 0) or 0), int(exp.get('holds', 0) or 0), int(exp.get('doubles', 0) or 0)
    want_taps = T + 2 * D
    if T or H or D:
        real = [p for p in presses if p['ms'] >= 10 and p['cls'] != 'long']
        if win.get('taps') or win.get('holds') or win.get('doubles'):
            it = [p for p in real if _in(p['t'], win.get('taps')) or _in(p['t'], win.get('doubles'))]
            ih = [p for p in real if _in(p['t'], win.get('holds'))]
        elif len(real) == want_taps + H:
            srt = sorted(real, key=lambda p: p['ms'])
            it, ih = srt[:want_taps], srt[want_taps:]
        else:
            it, ih = [], []
            f.append(_finding('presses', len(real), f'{len(real)} presses for {want_taps + H} asked: which were taps '
                                                    'and which holds is not clear, so no timing is suggested.'))
        tap_min, hold = tv('enc.tap_min'), tv('enc.hold')
        if it:
            short = [p for p in it if p['cls'] == 'bounce']
            ms = [p['ms'] for p in it]
            metrics['intended_tap_ms'] = sorted(ms)
            if short:
                s = min(p['ms'] for p in short)
                new = max(0, int(math.floor((s - 10) / 5.0) * 5))
                if new < tap_min:
                    sug['enc.tap_min'] = _snap('enc.tap_min', new)
                    f.append(_finding('tap min', s, f'the shortest tap {s} ms: under enc.tap_min {tap_min:.0f}, '
                                                    f'lower it to {new}.'))
        if it or ih:
            mt = max((p['ms'] for p in it), default=None)
            mh = min((p['ms'] for p in ih), default=None)
            metrics['longest_tap_ms'], metrics['shortest_hold_ms'] = mt, mh
            bad = (mt is not None and mt >= hold) or (mh is not None and mh < hold)
            if bad:
                if mt is not None and mh is not None and mt < mh:
                    new = (mt + mh) / 2.0
                elif mt is not None and mh is None:
                    new = mt * 1.3
                elif mh is not None and mt is None:
                    new = mh * 0.75
                else:
                    new = None
                if new is not None:
                    new = _snap('enc.hold', round(new / 10.0) * 10)
                    sug['enc.hold'] = new
                    f.append(_finding('hold', new, (f'your longest tap {mt} ms' if mt is not None else 'no taps') +
                                      (f' and shortest hold {mh} ms' if mh is not None else '') +
                                      f': enc.hold {hold:.0f} splits them wrongly, {new} sits between.'))
                else:
                    f.append(_finding('hold', None, f'taps up to {mt} ms and holds from {mh} ms overlap: no single '
                                                    'enc.hold tells them apart.'))
            elif mt is not None or mh is not None:
                f.append(_finding('hold', hold, f'enc.hold {hold:.0f} ms sits between your taps (up to {mt} ms) and '
                                                f'holds (from {mh} ms).'))
        if D:
            got_d = metrics['doubles']
            if got_d < D:
                pool = [p['t'] for p in taps if _in(p['t'], win.get('doubles'))] if win.get('doubles') else tap_t
                gaps = sorted((b - a) * 1000 for a, b in zip(pool, pool[1:]))[:D]
                cur = tv('enc.double')
                if len(gaps) == D and max(gaps) < 800:
                    new = _snap('enc.double', math.ceil(max(gaps) * 1.3 / 10.0) * 10)
                    if new > cur:
                        sug['enc.double'] = new
                        f.append(_finding('double tap', new, f'asked {D} double taps, counted {got_d}: your gaps reach '
                                                             f'{max(gaps):.0f} ms, over enc.double {cur:.0f}: raise it '
                                                             f'to {new}.'))
                else:
                    f.append(_finding('double tap', got_d, f'asked {D} double taps, counted {got_d}.'))
            elif got_d > D:
                f.append(_finding('double tap', got_d, f'asked {D} double taps, counted {got_d}: single taps came too '
                                                       'close together, or enc.double is long.'))
    if exp.get('longs') is not None and int(exp['longs']) != metrics['longs']:
        f.append(_finding('long press', metrics['longs'], f'asked {exp["longs"]}, counted {metrics["longs"]} '
                                                         f'(enc.long {tv("enc.long"):.0f} ms).'))
    return _result(metrics, f, sug)


def rec_stats(events, expected: dict | None = None, tune: dict | None = None) -> dict:
    """REC: down/up pairs, press lengths, bounces (presses under 30 ms). expected: {"short": 5, "long": 2}."""
    evs, tdef = _events_of(events)
    tv = lambda n: float((tune or {}).get(n, tdef(n)))
    presses, down = [], None
    for e in evs:
        n = e.get('name')
        if n == 'rec.down':
            down = e
        elif n == 'rec.up' and down is not None:
            ms = int(e.get('ms') or 0) or int(round((float(e.get('t', 0)) - float(down.get('t', 0))) * 1000))
            presses.append({'t': float(down.get('t', 0)), 'ms': ms})
            down = None
    ms = sorted(p['ms'] for p in presses)
    bounces = [m for m in ms if m < 30]
    metrics = {'presses': len(presses), 'press_ms': ms, 'bounces': len(bounces),
               'shortest_ms': ms[0] if ms else None, 'longest_ms': ms[-1] if ms else None,
               'median_ms': _f(np.median(ms), 0) if ms else None, 'unreleased': down is not None}
    f, sug = [], {}
    f.append(_finding('presses', len(presses), f'{len(presses)} presses' + (f', {ms[0]} to {ms[-1]} ms.' if ms else '.')))
    for n in ('rec.stuck', 'rec.reset'):
        c = sum(1 for e in evs if e.get('name') == n)
        if c:
            metrics[n.replace('.', '_')] = c
            f.append(_finding(n, c, 'REC held from power-on: ' + ('taken as stuck.' if n == 'rec.stuck' else 'factory reset.')))
    deb = tv('rec.debounce')
    if bounces:
        new = _snap('rec.debounce', math.ceil((max(bounces) + 10) / 10.0))
        if new > deb:
            sug['rec.debounce'] = new
        f.append(_finding('bounce', len(bounces), f'{len(bounces)} presses under 30 ms (the longest {max(bounces)} ms): '
                                                  f'the button chatters past rec.debounce {deb:.0f} ticks'
                                                  + (f'; {new} ticks would hold them.' if new > deb else '.')))
    if expected:
        want = int(expected.get('short', 0) or 0) + int(expected.get('long', 0) or 0)
        if want and len(presses) < want:
            f.append(_finding('missed', want - len(presses), f'asked for {want} presses, saw {len(presses)}: a press '
                                                             f'shorter than rec.debounce x 10 ms ({deb * 10:.0f} ms) '
                                                             'is not seen.'))
        elif want and len(presses) > want:
            f.append(_finding('extra', len(presses) - want, f'asked for {want} presses, saw {len(presses)}: '
                                                            'the button bounced or was pressed more.'))
        nl = int(expected.get('long', 0) or 0)
        if nl and len(ms) >= nl + 1:
            metrics['short_ms'], metrics['long_ms'] = ms[:-nl], ms[-nl:]
    if down is not None:
        f.append(_finding('unreleased', True, 'REC was still down at the end of the session.'))
    return _result(metrics, f, sug)


def joystick_stats(sess: Session, expected: dict | None = None) -> dict:
    """The joystick from state.csv (joy_rx, joy_ry raw ADC; joy_x, joy_y after shaping) and joy.flick events: the raw
    centre and its noise, the travel reached each way, a dead zone that clears the noise, flicks by direction.
    expected: {"flicks": 4, "window": (t0, t1)} counts the flicks asked for inside the window, the rest apart."""
    rx, ry = sess.col('joy_rx'), sess.col('joy_ry')
    jx, jy = sess.col('joy_x'), sess.col('joy_y')
    w = (expected or {}).get('window')
    every = [e for e in sess.events if e.get('name') == 'joy.flick']
    flick_ev = [e for e in every if w is None or _in(float(e.get('t', 0)), w)]
    by = {d: sum(1 for e in flick_ev if e.get('text') == d) for d in ('left', 'right', 'up', 'down')}
    metrics, f, sug = {'flicks': by, 'flicks_total': len(flick_ev), 'flicks_elsewhere': len(every) - len(flick_ev)}, [], {}
    if rx is None or ry is None or len(rx) == 0 or (np.nanmax(np.abs(rx)) == 0 and np.nanmax(np.abs(ry)) == 0):
        f.append(_finding('joystick', None, 'No joystick readings in this session (joy.on is off, or no joystick).'))
        return _result(metrics, f)
    rx, ry = np.asarray(rx, np.float64), np.asarray(ry, np.float64)
    dead = float(sess.tune('joy.dead', 0.12))
    if jx is not None and jy is not None:
        rest = (np.abs(jx) < 1e-3) & (np.abs(jy) < 1e-3)
    else:
        rest = np.zeros(len(rx), dtype=bool)
    if rest.sum() < 10:                    # no shaped values: the samples nearest the overall median
        d = np.hypot(rx - np.median(rx), ry - np.median(ry))
        rest = d < np.percentile(d, 20)
    cx, cy = float(np.median(rx[rest])), float(np.median(ry[rest]))
    metrics.update({'centre': [_f(cx, 1), _f(cy, 1)], 'rest_samples': int(rest.sum()),
                    'rest_spread': [_f(_p2p(rx[rest]), 1), _f(_p2p(ry[rest]), 1)]})
    norm = lambda v, c: np.where(v > c, (v - c) / (4095.0 - c), (v - c) / c)
    nx, ny = norm(rx, cx), norm(ry, cy)
    reach = {'raw x +': float(nx.max()), 'raw x -': float(-nx.min()), 'raw y +': float(ny.max()), 'raw y -': float(-ny.min())}
    metrics['reach'] = {k: _f(v, 3) for k, v in reach.items()}
    metrics['range'] = {'x': [_f(rx.min(), 0), _f(rx.max(), 0)], 'y': [_f(ry.min(), 0), _f(ry.max(), 0)]}
    f.append(_finding('centre', metrics['centre'], f'raw ADC at rest (of 0..4095), spread {metrics["rest_spread"][0]} '
                                                   f'and {metrics["rest_spread"][1]}.'))
    f.append(_finding('reach', metrics['reach'], 'share of the travel reached each way, raw axes, before the dead zone.'))
    noise = float(max(np.percentile(np.abs(nx[rest]), 99), np.percentile(np.abs(ny[rest]), 99)))
    metrics['rest_noise'] = _f(noise, 4)
    need = noise * 1.5 + 0.02
    metrics['dead_needed'] = _f(need, 3)
    if rest.sum() >= 50:
        if dead < need:
            sug['joy.dead'] = _snap('joy.dead', math.ceil(need * 100) / 100.0)
            f.append(_finding('dead zone', sug['joy.dead'], f'at rest the stick wanders {noise:.3f} of its travel: '
                                                            f'joy.dead {dead:.2f} lets that through, '
                                                            f'{sug["joy.dead"]:.2f} would not.'))
        elif dead > 2.5 * need + 0.05:     # said, not suggested: rest noise does not show how well it re-centres
            f.append(_finding('dead zone', dead, f'joy.dead {dead:.2f} is far above the rest noise ({noise:.3f}): a '
                                                 'smaller one would give back travel, if the stick also comes back to '
                                                 'the same centre after a push.'))
        else:
            f.append(_finding('dead zone', dead, f'joy.dead {dead:.2f} clears the rest noise ({noise:.3f}).'))
    if jx is not None and jy is not None:
        shaped = {'right': float(np.max(jx)), 'left': float(-np.min(jx)), 'up': float(np.max(jy)), 'down': float(-np.min(jy))}
        metrics['shaped_reach'] = {k: _f(v, 3) for k, v in shaped.items()}
        fl = float(sess.tune('joy.flick', 0.7))
        short = {k: v for k, v in shaped.items() if v < fl}
        if short:
            f.append(_finding('flick reach', metrics['shaped_reach'], 'after shaping, ' + ', '.join(
                f'{k} reaches {v:.2f}' for k, v in short.items()) + f': under joy.flick {fl:.2f}, no flick there.'))
            want = (expected or {}).get('flicks')
            lo = min(short.values())
            if want and lo - 0.05 >= 0.3:
                sug['joy.flick'] = _snap('joy.flick', math.floor((lo - 0.05) / 0.05) * 0.05)
    want = (expected or {}).get('flicks')
    if want is not None:
        f.append(_finding('flicks', len(flick_ev), f'asked for {want}, counted {len(flick_ev)} ('
                          + ', '.join(f'{k} {v}' for k, v in by.items()) + ').'))
    if metrics['flicks_elsewhere']:
        f.append(_finding('flicks elsewhere', metrics['flicks_elsewhere'],
                          'flicks outside the flick step: pushing the stick to its edge and back fires one too '
                          f'(past joy.flick {float(sess.tune("joy.flick", 0.7)):.2f}, ready again inside '
                          f'joy.rearm {float(sess.tune("joy.rearm", 0.3)):.2f}).'))
    return _result(metrics, f, sug)


# ---------------------------------------------------------------- comparing two analyses
LOWER = ('std', 'noise', 'rms', 'jitter', 'flutter', 'cents', 'lag', 'delay_', '_63_ms', '_90_ms', 'overshoot',
         'settle', 'drift', 'err', 'bounce', 'saturat', 'clip', 'p2p', 'allan', 'gap', 'overflow', 'lost', 'misread',
         'pitch_ratio', 'i2c', 'reversal', 'largest_step')


def _flat(d, pre=''):
    out = {}
    for k, v in (d or {}).items():
        key = f'{pre}{k}'
        if isinstance(v, dict):
            out.update(_flat(v, key + '.'))
        elif isinstance(v, bool):
            continue
        elif isinstance(v, (int, float)) and v is not None:
            out[key] = float(v)
    return out


def compare(a: dict, b: dict, tol: float = 0.02) -> list[dict]:
    """Two analyses side by side: rows of {metric, A, B, better}. better is "A", "B", "same" or "" (no rule says
    which way is better). Lower wins for noise, lag, drift, errors and the like; magnitudes compare as absolute values."""
    fa, fb = _flat(a.get('metrics', a)), _flat(b.get('metrics', b))
    rows = []
    for k in [k for k in fa if k in fb] + [k for k in fb if k not in fa]:
        va, vb = fa.get(k), fb.get(k)
        better = ''
        low = k.lower()
        if va is not None and vb is not None and any(w in low for w in LOWER):
            x, y = abs(va), abs(vb)
            if abs(x - y) <= tol * max(x, y, 1e-9):
                better = 'same'
            else:
                better = 'A' if x < y else 'B'
        rows.append({'metric': k, 'A': _f(va, 4), 'B': _f(vb, 4), 'better': better})
    return rows
