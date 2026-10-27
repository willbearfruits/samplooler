"""The guided tests: what the player is asked to do, step by step, and how each recording is read afterwards.

Each Test has steps (a label, a short prompt, seconds) and an analyser that takes the recorded Session and returns
{"test", "summary" (one plain sentence), "findings", "metrics", "suggest"}. A step can carry `synth` (the arguments
for synth.gesture that act it out, so the daemon can inject it into the twin or a board) or `keys` ((t offset s,
"enc" | "enc_down" | "rec" | "joy", value) to play through the board's key injection 'K'). The step windows come from
the session's meta test.steps (t0, t1, label); without them, from the steps' seconds laid end to end from t = 0.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable

import numpy as np

from . import analysis as A
from . import synth


@dataclass
class Step:
    label: str
    prompt: str
    secs: float
    synth: dict | None = None              # kwargs for synth.gesture (with "kind")
    keys: list | None = None               # [(t offset s, "enc" | "enc_down" | "rec" | "joy", value)]
    target: dict | None = None             # what the hold should read: {"pitch": .., "roll": ..}


@dataclass
class Test:
    title: str
    why: str
    steps: list[Step]
    analyze: Callable = field(repr=False, default=None)

    @property
    def secs(self) -> float:
        return float(sum(s.secs for s in self.steps))


# ---------------------------------------------------------------- the steps' windows
def windows(sess: A.Session, name: str) -> list[dict]:
    """The steps of test `name` in this session: [{label, prompt, t0, t1, target}], from meta or laid end to end."""
    test = TESTS[name]
    got = sess.steps()
    out, t = [], 0.0
    for i, st in enumerate(test.steps):
        m = next((g for g in got if g.get('label') == st.label and not g.get('_used')), None)
        if m is None and i < len(got) and got[i].get('t0') is not None and not got[i].get('_used'):
            m = got[i]
        if m is not None and m.get('t0') is not None:
            m['_used'] = True
            t0, t1 = float(m['t0']), float(m.get('t1', float(m['t0']) + st.secs))
        else:
            t0, t1 = t, t + st.secs
        t = t1
        out.append({'label': st.label, 'prompt': st.prompt, 't0': t0, 't1': t1, 'target': st.target or {}})
    return out


def _step(w: list[dict], label: str) -> dict:
    return next(s for s in w if s['label'] == label)


def _trim(s: dict, head: float, frac: float = 0.4) -> tuple[float, float]:
    """A step's window without its first moments (the hand moving into the pose)."""
    return s['t0'] + min(head, frac * (s['t1'] - s['t0'])), s['t1']


def _out(name: str, summary: str, findings: list, metrics: dict, suggest: dict | None = None, sess=None) -> dict:
    if sess is not None:
        q = A.stream_quality(sess)
        findings = q['findings'] + findings
        metrics = {**metrics, 'stream': q['metrics']}
    return A.jsonable({'test': name, 'summary': summary, 'findings': findings, 'metrics': metrics,
                       'suggest': suggest or {}})


# ---------------------------------------------------------------- analysers
def _still(sess):
    w = windows(sess, 'still')
    t0, t1 = _trim(w[0], 1.0, 0.2)
    r = A.rest_stats(sess, t0, t1, flat=True)
    d = A.drift(sess, t0, t1)
    ax = A.axis_check(sess, [('flat', t0, t1)])
    m = r['metrics']
    if m.get('n', 0) < 20:
        return _out('still', 'Too little motion was recorded to measure anything.', r['findings'], m, {}, sess)
    zf = [f for f in ax['findings'] if f['metric'] == 'device z']
    z_ok = ax['current_ok']
    b, nz = m['gyro_bias_chip'], m['gyro_std']
    summary = (f'Gyro bias {b[0]:+.2f} {b[1]:+.2f} {b[2]:+.2f} deg/s (chip x y z), gyro noise {max(nz):.2f} deg/s, '
               f'tilt noise {max(m["pitch_acc_noise"], m["roll_acc_noise"]):.2f} deg; '
               f'the z axis is {"right" if z_ok else "wrong"}.')
    if not m.get('still'):
        summary = 'The board moved during the still test: lay it down and let go, then run it again.'
    metrics = {**m, 'drift': d['metrics'], 'z_axis_ok': z_ok}
    return _out('still', summary, r['findings'] + zf + d['findings'][:1], metrics, r['suggest'], sess)


def _axes(sess):
    w = windows(sess, 'axes')
    segs = [{'label': s['label'], 't0': _trim(s, 1.5)[0], 't1': s['t1']} for s in w]
    r = A.axis_check(sess, segs)
    m = r['metrics']
    if r.get('axis_map') is None:
        return _out('axes', 'No usable holds were recorded.', r['findings'], m, {}, sess)
    known = [a for a, d in zip(A.AXES, r['determined']) if d]
    if r['current_ok']:
        summary = f'The axis map is right ({"/".join(m["suggested_codes"])}): checked on {", ".join(known)}.'
    else:
        summary = (f'The axis map is wrong: the board needs {"/".join(m["suggested_codes"])} for x/y/z, it has '
                   f'{"/".join(m["current_codes"])}.')
    return _out('axes', summary, r['findings'], {**m, 'axis_map': r['axis_map'], 'current_ok': r['current_ok']},
                r['suggest'], sess)


def _steps(sess):
    w = windows(sess, 'steps')
    hs = A.holds(sess, [{'label': s['label'], 't0': s['t0'], 't1': s['t1'], **s['target']} for s in w])
    rows = hs['metrics']['steps']
    # each move: the hand's own (gravity alone, every sample) against the board's pitch; the gap is the board's lag
    resp = []
    st = sess.state
    if 't' in st and sess.col('pitch') is not None:
        mt = sess.motion['t']
        gp = A.acc_angles(sess.acc - sess.acc_offset_dev())[0] if len(mt) else None
        prev = None
        for s, row in zip(w, rows):
            if prev is not None and row.get('pitch') is not None and abs(row['pitch'] - prev) > 3:
                r = A.step_response(st['t'], sess.col('pitch'), s['t0'], row['pitch'], t_end=s['t1'])['metrics']
                one = {'label': s['label'], 'board_63_ms': r.get('delay_63_ms'), 'board_90_ms': r.get('delay_90_ms'),
                       'overshoot_pct': r.get('overshoot_pct')}
                if gp is not None and row.get('pitch_acc') is not None:
                    h = A.step_response(mt, gp, s['t0'], row['pitch_acc'], t_end=s['t1'])['metrics']
                    one['hand_63_ms'] = h.get('delay_63_ms')
                    if one['board_63_ms'] is not None and one['hand_63_ms'] is not None:
                        one['lag_ms'] = A._f(one['board_63_ms'] - one['hand_63_ms'], 1)
                resp.append(one)
            prev = row.get('pitch', prev)
    dead, hyst = float(sess.tune('tilt.dead', 5)), float(sess.tune('tilt.hyst', 0))
    in_dead = [abs(r['pitch']) for r in rows if r.get('zone') == 'dead' and r.get('pitch') is not None]
    out_dead = [abs(r['pitch']) for r in rows if r.get('zone') in ('fwd', 'back', 'detent') and r.get('pitch') is not None]
    f = list(hs['findings'])
    if in_dead and out_dead:
        f.append(A._finding('dead zone', [round(max(in_dead), 1), round(min(out_dead), 1)],
                            f'the dead zone ends between {max(in_dead):.1f} and {min(out_dead):.1f} deg here; '
                            f'tilt.dead is {dead:g} deg (+{hyst:g} to leave it). The sweep test finds the edge exactly.'))
    agree = [abs(r['pitch_vs_acc']) for r in rows if r.get('pitch_vs_acc') is not None]
    noise = [r['pitch_std'] for r in rows if r.get('pitch_std') is not None]
    lags = [r['lag_ms'] for r in resp if r.get('lag_ms') is not None]
    if lags:
        f.append(A._finding('lag', A._f(np.median(lags), 0), 'ms the board\'s pitch reaches 63 % of each move after '
                                                             'gravity alone does (median of the moves; the hand\'s own '
                                                             'speed is taken out).'))
    speeds = ', '.join(f'{r["speed"]:.2f}x at {r["pitch"]:+.0f}' for r in rows if r.get('speed') is not None and r.get('pitch') is not None)
    summary = ('The board\'s pitch agrees with gravity within ' + (f'{max(agree):.2f} deg' if agree else '?') +
               (f', noise up to {max(noise):.2f} deg rms' if noise else '') +
               (f', {np.median(lags):.0f} ms behind each move' if lags else '') + (f'; speeds {speeds}.' if speeds else '.'))
    return _out('steps', summary, f, {'holds': rows, 'responses': resp, 'tilt_dead': dead}, {}, sess)


def _sweep(sess):
    w = windows(sess, 'sweep')
    s = _step(w, 'sweep')
    st = sess.state
    p, sp, z = sess.col('pitch'), sess.col('speed_cmd'), sess.col('zone')
    if 't' not in st or p is None or sp is None:
        return _out('sweep', 'No state was recorded: the curve cannot be read.', [], {}, {}, sess)
    m = A._win(st['t'], s['t0'], s['t1'])
    t, p, sp = st['t'][m], p[m], sp[m]
    zz = np.round(z[m]).astype(int) if z is not None else None
    metrics, f = {'pitch_from': A._f(p[:5].mean() if len(p) else None, 1), 'pitch_to': A._f(p[-5:].mean() if len(p) else None, 1),
                  'secs': A._f(t[-1] - t[0] if len(t) > 1 else 0, 2)}, []
    if len(p) < 20:
        return _out('sweep', 'Too short a sweep to read the curve.', [], metrics, {}, sess)
    rate = np.polyfit(t - t[0], p, 1)[0]
    metrics['rate_deg_s'] = A._f(rate, 2)
    edges = []
    if zz is not None:
        ch = np.nonzero(np.diff(zz) != 0)[0]
        for i in ch:
            edges.append({'t': A._f(t[i + 1], 3), 'pitch': A._f(p[i + 1], 2), 'from': A.ZONES[min(zz[i], 2)],
                          'to': A.ZONES[min(zz[i + 1], 2)]})
        metrics['zone_edges'] = edges
    dead = float(sess.tune('tilt.dead', 5))
    if edges:
        f.append(A._finding('dead zone', [e['pitch'] for e in edges], 'deg where the zone changed: ' +
                            ', '.join(f'{e["from"]} to {e["to"]} at {e["pitch"]:+.1f}' for e in edges) +
                            f' (tilt.dead {dead:g}).'))
    # smoothness: the tick-to-tick wobble of the speed in cents (second differences take the curve's own bend out:
    # white noise of s cents gives second differences of s * sqrt(6)), and ticks that went against the sweep
    a = np.abs(sp)
    ok = a > 1e-3
    c = 1200 * np.log2(np.maximum(a, 1e-3))
    live = ok & (zz != 0) if zz is not None else ok
    d2 = c[2:] - 2 * c[1:-1] + c[:-2]
    keep = live[2:] & live[1:-1] & live[:-2]
    if zz is not None:                     # not across a zone change
        keep &= (zz[2:] == zz[1:-1]) & (zz[1:-1] == zz[:-2])
    metrics['flutter_cents'] = A._f(d2[keep].std() / math.sqrt(6), 2) if keep.sum() > 5 else None
    sgn = np.sign(rate)
    back = np.diff(sp) * sgn < -0.02 * np.maximum(np.abs(sp[1:]), 0.05)
    metrics['reversals'] = int(back.sum())
    jumps = np.abs(np.diff(sp))
    metrics['largest_step'] = A._f(jumps.max(), 4)
    metrics['speed_range'] = [A._f(sp.min(), 3), A._f(sp.max(), 3)]
    fc = metrics['flutter_cents']
    word = None if fc is None else 'smooth' if fc < 3 else 'a slight wobble' if fc < 8 else 'an audible wobble'
    f.append(A._finding('smoothness', fc, f'cents rms of tick-to-tick wobble in the speed, outside the dead zone '
                                          f'({word}; it comes from the angle\'s noise times the curve\'s slope); '
                                          f'{metrics["reversals"]} ticks went against the sweep.'))
    f.append(A._finding('speed range', metrics['speed_range'], f'from {sp.min():.2f}x to {sp.max():.2f}x over '
                                                               f'{p.min():+.0f} to {p.max():+.0f} deg.'))
    summary = (f'Swept {p[0]:+.0f} to {p[-1]:+.0f} deg in {t[-1] - t[0]:.1f} s; '
               + (f'the dead zone ends at {", ".join(f"{e["pitch"]:+.1f}" for e in edges)} deg; ' if edges else '')
               + (f'the speed follows with {word} ({fc:.1f} cents rms) and {metrics["reversals"]} reversals.'
                  if fc is not None else 'the speed never left the dead zone.'))
    return _out('sweep', summary, f, metrics, {}, sess)


def _flicks(sess):
    w = windows(sess, 'flicks')
    rest, fl = _step(w, 'level'), _step(w, 'flicks')
    mt = sess.motion['t']
    mr = A._win(mt, *_trim(rest, 0.5))
    mf = A._win(mt, fl['t0'], fl['t1'])
    if mf.sum() < 20:
        return _out('flicks', 'No motion was recorded during the flicks.', [], {}, {}, sess)
    g = sess.gyro
    bias = g[mr].mean(0) if mr.sum() > 10 else np.zeros(3)
    rate = sess.rate_hz
    tf, prate = mt[mf], -(g[mf, 0] - bias[0])                 # nose down turns about -x: the pitch rate is -gx
    st, bp = sess.state.get('t'), sess.col('pitch')
    base = float(np.interp(tf[0], st, bp)) if st is not None and bp is not None and len(st) else 0.0
    ref = base + np.cumsum(prate) / rate                      # what the gyro says the pitch did
    amp_all = np.abs(ref - base)
    thr = 0.4 * amp_all.max()
    above = amp_all > thr
    d = np.diff(np.concatenate([[0], above.astype(int), [0]]))
    starts, ends = np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]
    lim = 32768 / sess.lsb[1]                                 # where the raw value clips
    gr = int(sess.tune('imu.gyro_range', 2))
    nominal = [250, 500, 1000, 2000][gr & 3]
    rail = np.zeros(int(mf.sum()), dtype=bool)               # gyro samples at the rail: the gyro's angle is short there
    if sess.raw is not None and len(sess.raw) == len(mt):
        rail = (np.abs(sess.raw[mf][:, 3:]) >= 32767).any(axis=1)
    flicks, f = [], []
    for s0, e0 in zip(starts, ends):
        pad = int(0.25 * rate)
        a, b = max(0, s0 - pad), min(len(tf), e0 + pad)
        seg_t, seg_ref = tf[a:b], ref[a:b]
        i_pk = int(np.argmax(np.abs(seg_ref - base)))
        row = {'t': A._f(seg_t[i_pk], 3), 'ref_peak': A._f(seg_ref[i_pk] - base, 2),
               'gyro_peak': A._f(np.abs(g[mf][a:b] - bias).max(), 1), 'clipped': bool(rail[a:b].any())}
        if st is not None and bp is not None and not row['clipped']:
            ms = A._win(st, seg_t[0], seg_t[-1] + 0.25)
            if ms.sum() > 3:
                sgn = np.sign(seg_ref[i_pk] - base)
                j = int(np.argmax((bp[ms] - base) * sgn))
                row['board_peak'] = A._f(bp[ms][j] - base, 2)
                row['reach_pct'] = A._f(100 * (bp[ms][j] - base) / (seg_ref[i_pk] - base), 1)
                # lag: the shift that best lines the board's pitch up with the gyro's, on a 1 ms grid
                grid = np.arange(seg_t[0], seg_t[-1], 0.001)
                rr = np.interp(grid, seg_t, seg_ref - base)
                bb = np.interp(grid, st[ms], bp[ms] - base)
                best, lag = -np.inf, 0
                for L in range(0, min(400, len(grid) // 2)):
                    c = float(np.dot(rr[:len(grid) - L], bb[L:]))
                    if c > best:
                        best, lag = c, L
                row['lag_ms'] = lag
                after = A._win(st, seg_t[-1], seg_t[-1] + 0.5)
                if after.sum():                # past the start on the way back, as a share of the flick
                    back = float(max(0.0, (-(bp[after] - base) * sgn).max()))
                    row['overshoot_pct'] = A._f(back / abs(seg_ref[i_pk] - base) * 100, 1)
        flicks.append(row)
    sat = A.saturation(sess, fl['t0'], fl['t1'])
    peaks = [r['gyro_peak'] for r in flicks if r.get('gyro_peak') is not None]
    lags = [r['lag_ms'] for r in flicks if r.get('lag_ms') is not None]
    reach = [r['reach_pct'] for r in flicks if r.get('reach_pct') is not None]
    metrics = {'flicks': flicks, 'count': len(flicks), 'gyro_peak_max': A._f(max(peaks), 1) if peaks else None,
               'gyro_range': nominal, 'saturated': sat['total'],
               'lag_ms_median': A._f(np.median(lags), 0) if lags else None,
               'reach_pct_median': A._f(np.median(reach), 1) if reach else None}
    sug = {}
    if sat['total'] or (peaks and max(peaks) > 0.9 * lim):
        if gr < 3:
            sug['imu.gyro_range'] = gr + 1
        f.append(A._finding('gyro range', metrics['gyro_peak_max'],
                            f'the flicks reached {max(peaks):.0f} deg/s of the {nominal} deg/s range'
                            + (f' and {sat["total"]} samples hit the rail' if sat['total'] else '') +
                            (f': imu.gyro_range up one step ({[250, 500, 1000, 2000][gr + 1]} deg/s).'
                             if gr < 3 else ': already the largest range.')))
    elif peaks:
        f.append(A._finding('gyro range', metrics['gyro_peak_max'], f'peaks up to {max(peaks):.0f} deg/s, inside the '
                                                                    f'{nominal} deg/s range.'))
    if lags:
        f.append(A._finding('lag', metrics['lag_ms_median'], f'ms the board\'s pitch trails the gyro\'s (median of '
                                                             f'{len(lags)} flicks).'))
    if reach:
        f.append(A._finding('reach', metrics['reach_pct_median'], '% of each flick\'s true size the board\'s pitch '
                                                                  'reached (smoothing cuts quick flicks short).'))
    sizes = [abs(r['ref_peak']) for r in flicks if not r['clipped']]
    summary = (f'{len(flicks)} flicks' + (f' of about {np.median(sizes):.0f} deg' if sizes else '') +
               (f': the board follows {metrics["lag_ms_median"]:.0f} ms late and reaches {metrics["reach_pct_median"]:.0f} % '
                'of the true peak' if lags and reach else '') +
               (f'; gyro peaks {max(peaks):.0f} of {nominal} deg/s' if peaks else '') +
               (', some clipped.' if sat['total'] else '.'))
    clipped = sum(1 for r in flicks if r['clipped'])
    if clipped:
        f.append(A._finding('clipped flicks', clipped, f'{clipped} of {len(flicks)} flicks clipped the gyro: its angle '
                                                       'comes out short there, so their lag and reach are not measured.'))
    return _out('flicks', summary, f, metrics, sug, sess)


def _shake(sess):
    w = windows(sess, 'shake')
    rest, sh = _step(w, 'rest'), _step(w, 'shake')
    r = A.shake_rejection(sess, _trim(sh, 0.5), _trim(rest, 0.5))
    m = r['metrics']
    s = m.get('shake', {})
    sp, seen, cents = s.get('pitch_p2p'), s.get('gyro_pitch_p2p'), s.get('speed_cents_p2p')
    fusion = int(sess.tune('motion.fusion', 0))
    name = ('ACC', 'COMP', 'MAHONY')[fusion] if 0 <= fusion <= 2 else str(fusion)
    if sp is None:
        summary = 'No state was recorded: the shake cannot be judged.'
    else:
        summary = (f'Shaking moved the pitch {sp:.1f} deg p2p' + (f' and the speed {cents:.0f} cents' if cents else '') +
                   (f'; the gyro saw {seen:.1f} deg of turning' if seen is not None else '') + f' ({name}).')
    return _out('shake', summary, r['findings'], m, r['suggest'], sess)


def _roll(sess):
    w = windows(sess, 'roll')
    st = sess.state
    rl, dl, su = sess.col('roll'), sess.col('delay'), sess.col('stutter')
    if 't' not in st or rl is None:
        return _out('roll', 'No state was recorded: the roll cannot be read.', [], {}, {}, sess)
    dead, full = float(sess.tune('roll.dead', 15)), float(sess.tune('roll.full', 45))
    f, metrics, sug = [], {'roll_dead': dead, 'roll_full': full}, {}
    parts = []
    for side, fx_name, fx in (('left', 'delay', dl), ('right', 'stutter', su)):
        s = _step(w, side)
        m = A._win(st['t'], s['t0'], s['t1'])
        if m.sum() < 3:
            continue
        tt, rr = st['t'][m], rl[m]
        k = tt >= _trim(s, 1.0, 0.5)[0]
        held = float(np.median(rr[k]))
        row = {'held_roll': A._f(held, 1), 'roll_std': A._f(rr[k].std(), 3)}
        other = su if fx_name == 'delay' else dl
        if fx is not None:
            row[fx_name] = A._f(np.median(fx[m][k]), 3)
            on = np.nonzero(fx[m] > 0)[0]
            if len(on):
                row['onset_roll'] = A._f(rr[on[0]], 1)
                row['onset_ms'] = A._f((tt[on[0]] - s['t0']) * 1000, 0)
            if other is not None and np.median(other[m][k]) > 0:
                row['wrong_effect'] = True
        metrics[side] = row
        want = -1 if side == 'left' else 1
        if row.get('wrong_effect') or np.sign(held) == -want and abs(held) > 5:
            f.append(A._finding(side, row['held_roll'], f'Rolled {side} but the roll reads {held:+.0f} deg: it gave the '
                                                        f'{"stutter" if side == "left" else "delay"}. Check the axis map '
                                                        '(the axes test) or roll.sign.'))
        elif abs(held) < dead:
            new = A._snap('roll.dead', max(0.0, math.floor(abs(held) * 0.6)))
            sug['roll.dead'] = min(new, sug.get('roll.dead', new))
            f.append(A._finding(side, row['held_roll'], f'Rolled {side} to {held:+.0f} deg: inside roll.dead {dead:g}, '
                                                        f'the {fx_name} never started. roll.dead {sug["roll.dead"]:g} '
                                                        'would catch a roll like this.'))
        else:
            amt = row.get(fx_name)
            f.append(A._finding(side, row['held_roll'], f'Rolled {side} to {held:+.0f} deg: {fx_name} {amt:.2f}'
                                + (f', started at {row["onset_roll"]:+.0f} deg, {row["onset_ms"]:.0f} ms into the step.'
                                   if row.get('onset_roll') is not None else '.')))
            parts.append(f'{side} {held:+.0f} deg gives {fx_name} {amt:.2f}')
    summary = ('; '.join(parts) + f' (roll.dead {dead:g}, roll.full {full:g}).') if parts else \
        'The roll did not reach the effects.'
    return _out('roll', summary[0].upper() + summary[1:], f, metrics, sug, sess)


def _encoder(sess):
    w = windows(sess, 'encoder')
    pad = 0.3
    win = {s['label']: (s['t0'], s['t1'] + pad) for s in w}
    exp = {'right': 10, 'left': 10, 'taps': 5, 'holds': 3, 'doubles': 2, 'windows': win}
    r = A.encoder_stats(sess, exp)
    m = r['metrics']
    bits = [f'counted {m.get("counted_right", 0):+.0f} right and {m.get("counted_left", 0):+.0f} left',
            f'{m["taps"]} taps, {m["holds"]} holds, {m["doubles"]} double taps, {m["bounces"]} bounces']
    summary = ('; '.join(bits) + '.') if not r['suggest'] else \
        ('; '.join(bits) + '. Suggested: ' + ', '.join(f'{k} {v}' for k, v in r['suggest'].items()) + '.')
    return _out('encoder', summary[0].upper() + summary[1:], r['findings'], m, r['suggest'], sess)


def _rec(sess):
    r = A.rec_stats(sess, {'short': 5, 'long': 2})
    m = r['metrics']
    summary = (f'{m["presses"]} presses' + (f', {m["shortest_ms"]} to {m["longest_ms"]} ms' if m['presses'] else '') +
               f', {m["bounces"]} bounces' + (' (asked for 7).' if m['presses'] != 7 else '.'))
    return _out('rec', summary, r['findings'], m, r['suggest'], sess)


def _joystick(sess):
    fl = _step(windows(sess, 'joystick'), 'flicks')
    r = A.joystick_stats(sess, {'flicks': 4, 'window': (fl['t0'], fl['t1'] + 0.3)})
    m = r['metrics']
    if 'centre' not in m:
        return _out('joystick', 'No joystick readings were recorded (joy.on is off, or no joystick).', r['findings'],
                    m, {}, sess)
    reach = m.get('reach', {})
    summary = (f'Centre {m["centre"][0]:.0f}, {m["centre"][1]:.0f}; travel reached '
               f'{min(reach.values()) * 100:.0f} to {max(reach.values()) * 100:.0f} %; '
               f'{m["flicks_total"]} flicks (asked for 4).')
    return _out('joystick', summary, r['findings'], m, r['suggest'], sess)


def _free(sess):
    st = sess.state
    if 't' not in st or sess.col('pitch') is None:
        return _out('free', 'No state was recorded.', [], {}, {}, sess)
    s = _step(windows(sess, 'free'), 'play')
    m = A._win(st['t'], s['t0'], s['t1'])
    t = st['t'][m]
    p, r = sess.col('pitch')[m], sess.col('roll')[m]
    secs = float(t[-1] - t[0]) if len(t) > 1 else 0.0
    metrics = {'secs': A._f(secs, 1), 'pitch_range': [A._f(np.percentile(p, 5), 1), A._f(np.percentile(p, 95), 1)],
               'roll_range': [A._f(np.percentile(r, 5), 1), A._f(np.percentile(r, 95), 1)]}
    f = []
    z = sess.col('zone')
    if z is not None:
        zz = np.round(z[m]).astype(int)
        metrics['zone_share'] = {A.ZONES[k]: A._f(np.mean(zz == k), 3) for k in range(3)}
        metrics['zone_changes_per_min'] = A._f(np.sum(np.diff(zz) != 0) / max(secs / 60.0, 1e-6), 1)
    sp = sess.col('speed_cmd')
    if sp is not None:
        metrics['speed_range'] = [A._f(np.percentile(sp[m], 5), 3), A._f(np.percentile(sp[m], 95), 3)]
    for k in ('delay', 'stutter'):
        x = sess.col(k)
        if x is not None:
            metrics[f'{k}_share'] = A._f(np.mean(x[m] > 0), 3)
    names = {}
    for e in sess.events:
        if s['t0'] <= float(e.get('t', 0)) <= s['t1'] + 0.3:
            names[e.get('name')] = names.get(e.get('name'), 0) + 1
    metrics['events'] = names
    full = float(sess.tune('tilt.full', 45))
    f.append(A._finding('pitch used', metrics['pitch_range'], f'deg, 5th to 95th percentile (tilt.full {full:g}).'))
    if 'zone_share' in metrics:
        f.append(A._finding('zones', metrics['zone_share'], 'share of the time in the dead zone, forward and back.'))
    summary = (f'{secs:.0f} s of play: pitch {metrics["pitch_range"][0]:+.0f} to {metrics["pitch_range"][1]:+.0f} deg'
               + (f', {metrics["zone_share"]["dead"] * 100:.0f} % in the dead zone' if 'zone_share' in metrics else '')
               + (f', speeds {metrics["speed_range"][0]:.2f} to {metrics["speed_range"][1]:.2f}x' if 'speed_range' in metrics else '')
               + '. Replay it with other tuning to compare.')
    return _out('free', summary, f, metrics, {}, sess)


# ---------------------------------------------------------------- the tests
def _tilt(pitch=0.0, roll=0.0, fp=0.0, fr=0.0, start=0.3, ramp=0.6) -> dict:
    return {'kind': 'tilt', 'pitch': pitch, 'roll': roll, 'from_pitch': fp, 'from_roll': fr, 'start': start, 'ramp': ramp}


def _hold_at(pitch=0.0, roll=0.0) -> dict:
    return {'kind': 'still', 'pitch': pitch, 'roll': roll}


def _turns(n: int, d: int, t0=0.5, gap=0.4) -> list:
    return [(round(t0 + gap * i, 3), 'enc', d) for i in range(n)]


def _presses(kind: str, at: list, length: float) -> list:
    out = []
    for t in at:
        out += [(round(t, 3), kind, True), (round(t + length, 3), kind, False)]
    return out


def _circle(t0=0.5, secs=6.0, turns=2, r=2000) -> list:
    n = int(secs / 0.05)
    out = []
    for i in range(n + 1):
        a = 2 * math.pi * turns * i / n
        out.append((round(t0 + i * 0.05, 3), 'joy', (int(2048 + r * math.cos(a)), int(2048 + r * math.sin(a)))))
    return out + [(round(t0 + secs + 0.3, 3), 'joy', (2048, 2048)), (round(t0 + secs + 0.8, 3), 'joy', None)]


def _flick_keys() -> list:
    # raw x and y as the module reads them; with joy.rotate 90 (v1) right is raw y up, up is raw x down
    pos = {'right': (2048, 4000), 'left': (2048, 100), 'up': (100, 2048), 'down': (4000, 2048)}
    out = []
    for i, d in enumerate(('right', 'left', 'up', 'down')):
        t = 0.5 + 1.6 * i
        out += [(round(t, 3), 'joy', pos[d]), (round(t + 0.15, 3), 'joy', (2048, 2048))]
    return out + [(7.5, 'joy', None)]


TESTS: dict[str, Test] = {
    'still': Test('Still', 'The gyro\'s bias and noise, the tilt\'s noise, and whether z points out of the screen.',
                  [Step('flat', 'Lay it flat on the table, screen up, and let go.', 10, _hold_at())], _still),
    'axes': Test('Axes', 'Which way the sensor sits on the board: the axis map, checked on all three axes.',
                 [Step('flat', 'Lay it flat, screen up.', 4, _hold_at()),
                  Step('nose_down', 'Tip the top edge down, about 60 degrees, and hold.', 4, _tilt(60, ramp=0.8)),
                  Step('right_down', 'Now tip the right edge down instead, about 60 degrees, and hold.', 4,
                       _tilt(0, 60, fp=60, ramp=0.8)),
                  Step('facing', 'Stand it up with the screen facing you.', 4, _tilt(-90, 0, fr=60, ramp=0.8))], _axes),
    'steps': Test('Tilt steps', 'How exact and how calm each angle reads, how fast the board settles, and where the '
                                'dead zone ends.',
                  [Step('level', 'Hold it level.', 4, _hold_at(), target={'pitch': 0, 'roll': 0}),
                   Step('down 15', 'Tip the nose down a little, about 15 degrees, and hold.', 4, _tilt(15),
                        target={'pitch': 15, 'roll': 0}),
                   Step('down 30', 'Further, about 30 degrees, and hold.', 4, _tilt(30, fp=15),
                        target={'pitch': 30, 'roll': 0}),
                   Step('down 45', 'Further, about 45 degrees, and hold.', 4, _tilt(45, fp=30),
                        target={'pitch': 45, 'roll': 0}),
                   Step('level again', 'Back to level, and hold.', 4, _tilt(0, fp=45), target={'pitch': 0, 'roll': 0}),
                   Step('up 30', 'Tip the nose up, about 30 degrees, and hold.', 4, _tilt(-30),
                        target={'pitch': -30, 'roll': 0})], _steps),
    'sweep': Test('Slow sweep', 'The whole tilt curve in one go: where the dead zone ends and how smooth the speed is.',
                  [Step('nose up', 'Tip the nose up, about 45 degrees, and hold.', 3, _tilt(-45, ramp=0.8)),
                   Step('sweep', 'Slowly tip it nose down, all the way, over 8 seconds.', 9,
                        {'kind': 'sweep', 'a': -45, 'b': 45, 'pre': 0.5, 'post': 0.5})], _sweep),
    'flicks': Test('Flicks', 'Quick moves: how late and how short the board\'s angle is, and whether the gyro clips.',
                   [Step('level', 'Hold it level and still.', 3, _hold_at()),
                    Step('flicks', 'Flick the nose down and back, quickly, 5 times.', 6,
                         {'kind': 'flicks', 'n': 5, 'amp': 30, 'width': 0.15})], _flicks),
    'shake': Test('Shake', 'Shaking without tilting should leave the speed alone. With ACC it does not: the case for '
                           'COMP.',
                  [Step('rest', 'Hold it level and still.', 3, _hold_at()),
                   Step('shake', 'Shake it side to side and up and down. Keep it level.', 5,
                        {'kind': 'shake', 'amp': 0.5, 'start': 0.3, 'end': 4.7}),
                   Step('rest after', 'Stop, and hold it still.', 2, _hold_at())], _shake),
    'roll': Test('Roll', 'Where the delay (left) and the stutter (right) start, and how far your roll goes.',
                 [Step('level', 'Hold it level.', 2, _hold_at()),
                  Step('left', 'Roll it to the left and hold.', 4, _tilt(0, -35)),
                  Step('back', 'Back to level.', 2, _tilt(0, 0, fr=-35, start=0.2)),
                  Step('right', 'Roll it to the right and hold.', 4, _tilt(0, 35)),
                  Step('level again', 'Back to level.', 2, _tilt(0, 0, fr=35, start=0.2))], _roll),
    'encoder': Test('Encoder', 'Direction, counts per click, and the timing that tells a tap from a hold and a double '
                               'tap. The menu may open on the way: that is expected.',
                    [Step('right', 'Turn the knob 10 clicks to the right.', 6, keys=_turns(10, 1)),
                     Step('left', 'Turn it 10 clicks to the left.', 6, keys=_turns(10, -1)),
                     Step('taps', 'Tap the knob 5 times, a second apart.', 7,
                          keys=_presses('enc_down', [0.5 + 1.2 * i for i in range(5)], 0.12)),
                     Step('holds', 'Press and hold the knob for about a second. 3 times.', 8,
                          keys=_presses('enc_down', [0.5 + 2.4 * i for i in range(3)], 1.0)),
                     Step('doubles', 'Double-tap the knob. Twice, with a pause between.', 6,
                          keys=_presses('enc_down', [0.5, 0.75, 3.0, 3.25], 0.1))], _encoder),
    'rec': Test('REC button', 'Every press seen once, with its length, and no bounces.',
                [Step('short', 'Press REC briefly, 5 times.', 8,
                      keys=_presses('rec', [0.5 + 1.4 * i for i in range(5)], 0.15)),
                 Step('long', 'Hold REC for 2 seconds, then let go. Twice.', 9,
                      keys=_presses('rec', [0.5, 4.5], 2.0))], _rec),
    'joystick': Test('Joystick', 'Its centre and noise, how far it reaches each way, and whether every flick counts.',
                     [Step('circle', 'Circle the stick slowly round its edge, twice.', 8, keys=_circle()),
                      Step('flicks', 'Flick it right, left, up and down. Let go each time.', 8, keys=_flick_keys())],
                     _joystick),
    'free': Test('Free play', 'Your own playing, recorded to replay through other tuning and compare.',
                 [Step('play', 'Play it freely for 30 seconds: tilt, roll, flick and hold.', 30, {'kind': 'free'})], _free),
}


def describe() -> list[dict]:
    """The tests as data, for the viewer and the CLI."""
    return [{'name': n, 'title': t.title, 'why': t.why, 'secs': t.secs,
             'steps': [{'label': s.label, 'prompt': s.prompt, 'secs': s.secs, 'synth': s.synth is not None,
                        'keys': bool(s.keys)} for s in t.steps]} for n, t in TESTS.items()]


def analyze(sess, name: str | None = None) -> dict:
    """Run the analyser for this session's test (meta test.name, or `name`)."""
    if not isinstance(sess, A.Session):
        sess = A.load_session(sess)
    name = name or (sess.meta.get('test') or {}).get('name')
    if name not in TESTS:
        raise KeyError(f'no guided test {name!r}: one of {", ".join(TESTS)}')
    return TESTS[name].analyze(sess)


def act_out(name: str, rate_hz: float = 500, acc_range: int = 0, gyro_range: int = 2, seed: int = 0, **model) -> dict:
    """The whole test acted out by the synthetic hand: raw samples (chip axes, LSB), the steps' windows (meta's
    test.steps) and every key with its time from the start. model: synth.gesture's bias, offset, noise or mount."""
    test = TESTS[name]
    raws, steps, keys, t = [], [], [], 0.0
    pose = (0.0, 0.0)
    for i, s in enumerate(test.steps):
        kw = dict(s.synth) if s.synth else {'kind': 'still', 'pitch': pose[0], 'roll': pose[1]}
        kind = kw.pop('kind')
        raw, tr = synth.gesture(kind, s.secs, rate_hz, acc_range, gyro_range, seed=seed + i, with_truth=True,
                                **model, **kw)
        raws.append(raw)
        if len(tr['pitch']):
            pose = (float(tr['pitch'][-1]), float(tr['roll'][-1]))
        steps.append({'label': s.label, 'prompt': s.prompt, 't0': round(t, 4), 't1': round(t + s.secs, 4)})
        for (dt, k, v) in s.keys or []:
            keys.append((round(t + dt, 4), k, v))
        t += s.secs
    return {'raw': np.concatenate(raws) if raws else np.zeros((0, 6), np.int16),
            'test': {'name': name, 'steps': steps}, 'keys': keys}
