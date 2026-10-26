"""guided.py: the tests as data, acted out by the synthetic hand (synth), read back through the firmware's own motion and
gesture code (the twin) and analysed. Keys (encoder, REC, joystick) are turned into the events the board would send
by a small stand-in here, since the twin replays motion only."""
import json
import subprocess

import numpy as np
import pytest

from slrig import analysis as A
from slrig import guided as G
from slrig import synth

MOTION_TESTS = ['still', 'axes', 'steps', 'sweep', 'flicks', 'shake', 'roll', 'free']
KEY_TESTS = ['encoder', 'rec', 'joystick']


@pytest.fixture(scope='module')
def twin():
    if not synth.TWIN.exists():
        subprocess.run(['sh', str(synth.RIG / 'twin' / 'build.sh')], capture_output=True)
    if not synth.TWIN.exists():
        pytest.skip('the twin is not built (rig/twin/build.sh)')
    return synth.TWIN


def run(tmp, name, tune=None, axis_map=(3, 0, 4), with_meta=True, gyro_range=2, events=None, state=None, **model):
    act = G.act_out(name, gyro_range=gyro_range, **model)
    st = state if state is not None else synth.twin_states(act['raw'], tune=tune, gyro_range=gyro_range)
    p = synth.write_session(tmp / name, act['raw'], state=st, test=act['test'] if with_meta else None, tune=tune,
                            axis_map=axis_map, gyro_range=gyro_range, events=events)
    r = G.analyze(p, name)
    json.dumps(r, allow_nan=False)
    assert set(r) == {'test', 'summary', 'findings', 'metrics', 'suggest'} and r['test'] == name
    assert isinstance(r['summary'], str) and r['summary'].endswith('.') and len(r['summary']) < 400
    for f in r['findings']:
        assert set(f) == {'metric', 'value', 'note'} and f['note']
    return r


# ---------------------------------------------------------------- the tests as data
def test_every_test_is_complete():
    assert set(G.TESTS) == set(MOTION_TESTS + KEY_TESTS)
    for name, t in G.TESTS.items():
        assert t.title and t.why.endswith('.') and callable(t.analyze) and t.secs > 0
        labels = [s.label for s in t.steps]
        assert len(labels) == len(set(labels)), name
        for s in t.steps:
            assert s.prompt.endswith('.') and len(s.prompt) <= 90 and '!' not in s.prompt, s.prompt
            assert s.secs > 0
            assert (s.synth is None) or (s.keys is None)
            for (dt, kind, v) in s.keys or []:
                assert 0 <= dt <= s.secs and kind in ('enc', 'enc_down', 'rec', 'joy')
                if kind == 'enc':
                    assert isinstance(v, int) and v != 0
                elif kind == 'joy':
                    assert v is None or (len(v) == 2 and all(0 <= c <= 4095 for c in v))
                else:
                    assert isinstance(v, bool)
            downs = [v for (_, k, v) in s.keys or [] if k in ('enc_down', 'rec')]
            assert downs == [True, False] * (len(downs) // 2)        # every press is let go


def test_every_synth_step_runs():
    for name, t in G.TESTS.items():
        for s in t.steps:
            if s.synth:
                kw = dict(s.synth)
                raw = synth.gesture(kw.pop('kind'), s.secs, **kw)
                assert raw.shape == (int(round(s.secs * 500)), 6)


@pytest.mark.parametrize('name', MOTION_TESTS)
def test_act_out_has_no_jumps_between_steps(name):
    act = G.act_out(name, noise_g=0, noise_dps=0)
    a = synth.to_device(act['raw'])[:, :3]
    d = np.abs(np.diff(a, axis=0)).max(axis=1)
    for s in act['test']['steps'][1:]:                             # where one step's gesture meets the next
        i = int(round(s['t0'] * 500))
        assert d[i - 1] < 0.01, (name, s['label'])
    ends = [s['t1'] for s in act['test']['steps']]
    assert ends[-1] == pytest.approx(G.TESTS[name].secs) and len(act['raw']) == round(G.TESTS[name].secs * 500)


def test_describe_and_keys_timeline():
    d = G.describe()
    json.dumps(d)
    assert [x['name'] for x in d] == list(G.TESTS)
    act = G.act_out('encoder')
    ts = [k[0] for k in act['keys']]
    assert ts == sorted(ts) and act['keys'][0] == (0.5, 'enc', 1)
    assert act['keys'][10][0] == pytest.approx(6.5)               # the second step starts at 6 s


def test_unknown_test():
    with pytest.raises(KeyError):
        G.TESTS['nope']
    s = A.Session(path=__import__('pathlib').Path('.'), meta={'test': {'name': 'nope'}}, motion={}, raw=None, state={})
    with pytest.raises(KeyError):
        G.analyze(s)


# ---------------------------------------------------------------- motion tests through the twin
def test_still(tmp_path, twin):
    r = run(tmp_path, 'still')
    s = r['suggest']
    assert np.allclose([s[f'imu.gyro_bias_{a}'] for a in 'xyz'], [0.9, -0.6, 0.4], atol=0.05)
    assert np.allclose([s[f'imu.acc_off_{a}'] for a in 'xyz'], [0.010, -0.006, 0.020], atol=0.002)
    assert r['metrics']['z_axis_ok'] is True and 'z axis is right' in r['summary']


def test_still_without_meta_steps(tmp_path, twin):
    r = run(tmp_path, 'still', with_meta=False)
    assert r['metrics']['still'] is True


def test_axes(tmp_path, twin):
    assert run(tmp_path, 'axes')['metrics']['current_ok'] is True


def test_axes_wrong_map(tmp_path, twin):
    r = run(tmp_path, 'axes', axis_map=(2, 1, 4))
    assert r['suggest'] == {'motion.ax_x': 3, 'motion.ax_y': 0, 'motion.ax_z': 4} and 'wrong' in r['summary']


def test_axes_other_mounting(tmp_path, twin):
    r = run(tmp_path, 'axes', mount=(3, 1, 5))
    assert r['suggest'] == {'motion.ax_x': 3, 'motion.ax_y': 1, 'motion.ax_z': 5}


def test_steps(tmp_path, twin):
    tune = {'imu.acc_off_x': 0.010, 'imu.acc_off_y': -0.006, 'imu.acc_off_z': 0.020}
    r = run(tmp_path, 'steps', tune=tune)
    for h in r['metrics']['holds']:
        assert h['pitch'] == pytest.approx(h['target_pitch'], abs=0.5), h['label']
    lags = [x['lag_ms'] for x in r['metrics']['responses']]
    assert len(lags) == 5 and all(20 < x < 150 for x in lags)      # ACC's one-pole at 50 Hz: tens of ms
    assert any(f['metric'] == 'dead zone' for f in r['findings'])


def test_sweep(tmp_path, twin):
    r = run(tmp_path, 'sweep', tune={'imu.acc_off_x': 0.010, 'imu.acc_off_y': -0.006, 'imu.acc_off_z': 0.020})
    edges = r['metrics']['zone_edges']
    assert [e['to'] for e in edges] == ['dead', 'fwd']
    assert edges[0]['pitch'] == pytest.approx(-5, abs=0.5) and edges[1]['pitch'] == pytest.approx(5, abs=0.5)
    assert r['metrics']['reversals'] == 0 and r['metrics']['flutter_cents'] < 15


def test_sweep_with_hysteresis(tmp_path, twin):
    tune = {'tilt.hyst': 3, 'imu.acc_off_x': 0.010, 'imu.acc_off_y': -0.006, 'imu.acc_off_z': 0.020}
    edges = run(tmp_path, 'sweep', tune=tune)['metrics']['zone_edges']
    # coming back from nose up it leaves 'back' at -5; going on it needs 5 + 3 to leave the dead zone
    assert edges[0]['pitch'] == pytest.approx(-5, abs=0.5) and edges[1]['pitch'] == pytest.approx(8, abs=0.5)


def test_flicks(tmp_path, twin):
    r = run(tmp_path, 'flicks')
    m = r['metrics']
    assert m['count'] == 5 and m['saturated'] == 0 and r['suggest'] == {}
    assert m['gyro_peak_max'] == pytest.approx(np.pi * 30 / 0.15, rel=0.03)
    assert all(abs(f['ref_peak'] - 30) < 1.5 for f in m['flicks'])  # the gyro's own angle: the flick's true size
    assert 10 < m['lag_ms_median'] < 120 and 30 < m['reach_pct_median'] < 95


def test_flicks_clip_a_small_range(tmp_path, twin):
    r = run(tmp_path, 'flicks', gyro_range=0, tune={'imu.gyro_range': 0})
    assert r['suggest'] == {'imu.gyro_range': 1} and r['metrics']['saturated'] > 0
    assert r['metrics']['lag_ms_median'] is None                   # clipped flicks are not measured


def test_shake(tmp_path, twin):
    r = run(tmp_path, 'shake')
    assert r['suggest'] == {'motion.fusion': 1} and '(ACC)' in r['summary']
    c = run(tmp_path / 'c', 'shake', tune={'motion.fusion': 1})
    assert c['suggest'] == {} and c['metrics']['shake']['pitch_p2p'] < r['metrics']['shake']['pitch_p2p'] / 3


def test_roll(tmp_path, twin):
    r = run(tmp_path, 'roll')
    m = r['metrics']
    assert m['left']['delay'] > 0.5 and m['right']['stutter'] > 0.5 and r['suggest'] == {}
    assert m['left']['onset_roll'] == pytest.approx(-15, abs=1.5) and m['right']['onset_roll'] == pytest.approx(15, abs=1.5)


def test_roll_too_shy(tmp_path, twin):
    r = run(tmp_path, 'roll', tune={'roll.dead': 40})
    held = min(abs(r['metrics'][s]['held_roll']) for s in ('left', 'right'))
    assert r['suggest'] == {'roll.dead': np.floor(held * 0.6)} and 'did not reach' in r['summary']


def test_free(tmp_path, twin):
    r = run(tmp_path, 'free')
    m = r['metrics']
    assert m['secs'] == pytest.approx(30, abs=0.1) and sum(m['zone_share'].values()) == pytest.approx(1, abs=0.01)


# ---------------------------------------------------------------- key tests, with the board's events stood in
def board_events(keys, enc_sign=1, enc_scale=1, tune=None):
    """The events gesture.c and main.c would send for these keys (the rules of gest_enc, main.c's double tap and REC)."""
    tv = {**{k: A.tune_default(k) for k in ('enc.tap_min', 'enc.hold', 'enc.long', 'enc.double')}, **(tune or {})}
    ev, down, rec_down, last_tap = [], None, None, None
    for t, kind, v in keys:
        if kind == 'enc':
            ev.append({'t': t, 'name': 'enc.turn', 'v': v * enc_sign * enc_scale, 'ms': 0})
        elif kind == 'enc_down' and v:
            down = t
            ev.append({'t': t, 'name': 'enc.down', 'v': 0, 'ms': 0})
        elif kind == 'enc_down':
            ms = int(round((t - down) * 1000))
            ev.append({'t': t, 'name': 'enc.up', 'v': 0, 'ms': ms})
            cls = 'bounce' if ms < tv['enc.tap_min'] else 'tap' if ms < tv['enc.hold'] else 'hold'
            ev.append({'t': t, 'name': f'enc.{cls}', 'v': 0, 'ms': ms})
            if cls == 'tap':
                if last_tap is not None and (t - last_tap) * 1000 < tv['enc.double']:
                    ev.append({'t': t, 'name': 'enc.double', 'v': 0, 'ms': 0})
                    last_tap = None
                else:
                    last_tap = t
        elif kind == 'rec':
            ev.append({'t': t + 0.03, 'name': 'rec.down' if v else 'rec.up', 'v': 0, 'ms': 0})
    return ev


def test_encoder_right(tmp_path):
    act = G.act_out('encoder')
    r = run(tmp_path, 'encoder', events=board_events(act['keys']), state={})
    m = r['metrics']
    assert (m['counted_right'], m['counted_left']) == (10, -10)
    assert (m['taps'], m['holds'], m['doubles'], m['bounces']) == (9, 3, 2, 0)
    assert r['suggest'] == {}


def test_encoder_reversed_and_doubled(tmp_path):
    act = G.act_out('encoder')
    r = run(tmp_path, 'encoder', events=board_events(act['keys'], enc_sign=-1, enc_scale=2), state={})
    assert r['suggest'] == {'enc.dir': 1, 'enc.steps': 8}


def test_encoder_slow_fingers(tmp_path):
    act = G.act_out('encoder')
    tune = {'enc.hold': 1100, 'enc.double': 200}
    r = run(tmp_path, 'encoder', events=board_events(act['keys'], tune=tune), state={}, tune=tune)
    # holds of 1 s read as taps under enc.hold 1100; double taps 250 ms apart miss enc.double 200
    assert r['suggest']['enc.hold'] == 560 and r['suggest']['enc.double'] == 330


def test_rec(tmp_path):
    act = G.act_out('rec')
    r = run(tmp_path, 'rec', events=board_events(act['keys']), state={})
    m = r['metrics']
    assert m['presses'] == 7 and m['bounces'] == 0 and m['long_ms'] == [2000, 2000] and r['suggest'] == {}


def joy_state(keys, secs, noise=4.0):
    """The joystick's state.csv columns for these key overrides, by gesture.c's axis(), rotation 90, smoothing and flicks."""
    rng = np.random.default_rng(0)
    t = np.arange(0, secs, 0.02)
    rx, ry = np.full(len(t), 2048.0), np.full(len(t), 2048.0)
    for (t0, _, v), nxt in zip(keys, keys[1:] + [(secs, None, None)]):
        k = (t >= t0) & (t < nxt[0])
        if v is not None:
            rx[k], ry[k] = v
    rx += noise * rng.standard_normal(len(t))
    ry += noise * rng.standard_normal(len(t))

    def axis(raw):
        v = np.where(raw > 2048, (raw - 2048) / 2047.0, (raw - 2048) / 2048.0)
        a = np.abs(v)
        return np.where(a < 0.12, 0.0, np.clip((a - 0.12) / 0.88 * np.sign(v), -1, 1))
    ax, ay = axis(rx), axis(ry)
    x, y = ay, -ax
    jx, jy = np.zeros(len(t)), np.zeros(len(t))
    ev, armed = [], [True, True]
    for i in range(len(t)):
        jx[i] = (jx[i - 1] if i else 0) + 0.3 * (x[i] - (jx[i - 1] if i else 0))
        jy[i] = (jy[i - 1] if i else 0) + 0.3 * (y[i] - (jy[i - 1] if i else 0))
        for j, (v, neg, pos) in enumerate(((jx[i], 'left', 'right'), (jy[i], 'down', 'up'))):
            if abs(v) > 0.7 and armed[j]:
                ev.append({'t': t[i], 'name': 'joy.flick', 'v': v, 'text': pos if v > 0 else neg})
                armed[j] = False
            elif abs(v) < 0.3:
                armed[j] = True
    return {'t': t, 'joy_rx': rx, 'joy_ry': ry, 'joy_x': jx, 'joy_y': jy, 'flags': np.full(len(t), 32 | 512)}, ev


def test_joystick(tmp_path):
    act = G.act_out('joystick')
    st, ev = joy_state(act['keys'], G.TESTS['joystick'].secs)
    r = run(tmp_path, 'joystick', events=ev, state=st)
    m = r['metrics']
    assert m['flicks'] == {'left': 1, 'right': 1, 'up': 1, 'down': 1}
    assert m['flicks_elsewhere'] == 9                              # two turns from the right edge: right 3, the rest 2
    assert m['centre'] == pytest.approx([2048, 2048], abs=3)
    assert all(v > 0.95 for v in m['reach'].values()) and r['suggest'] == {}
