"""analysis.py on sessions with known answers: synthetic motion (synth.py) and the board's state from the firmware's own
motion.c and gesture.c (the twin's --calc replay)."""
import json
import struct
import subprocess

import numpy as np
import pytest

from slrig import analysis as A
from slrig import synth

CHIP_BIAS = np.array([0.9, -0.6, 0.4])
CHIP_OFF = np.array([0.010, -0.006, 0.020])
OFF_TUNE = {'imu.acc_off_x': 0.010, 'imu.acc_off_y': -0.006, 'imu.acc_off_z': 0.020}


@pytest.fixture(scope='module')
def twin():
    if not synth.TWIN.exists():
        subprocess.run(['sh', str(synth.RIG / 'twin' / 'build.sh')], capture_output=True)
    if not synth.TWIN.exists():
        pytest.skip('the twin is not built (rig/twin/build.sh)')
    return synth.TWIN


def session(path, raw, tune=None, twin_tune=None, axis_map=synth.MOUNT, with_state=True, **kw):
    """A session folder as the daemon writes it, with the board's state from the twin."""
    st = synth.twin_states(raw, tune=twin_tune if twin_tune is not None else tune,
                           gyro_range=kw.get('gyro_range', 2)) if with_state else None
    p = synth.write_session(path, raw, state=st, tune=tune, axis_map=axis_map, **kw)
    return A.load_session(p)


@pytest.fixture(scope='module')
def still(tmp_path_factory, twin):
    return session(tmp_path_factory.mktemp('s') / 'still', synth.gesture('still', 10))


# ---------------------------------------------------------------- loading
def test_tune_table_is_the_firmwares():
    t = A.tune_table()
    names = [d['name'] for d in t]
    assert names[0] == 'imu.acc_range' and 'tilt.dead' in names and len(names) == len(set(names))
    assert names.index('motion.ax_x') == 23
    assert A.tune_default('tilt.dead') == 5 and A.tune_default('enc.steps') == 4
    assert next(d for d in t if d['name'] == 'motion.ax_x')['labels'] == ['+x', '-x', '+y', '-y', '+z', '-z']


def test_load_session_reads_every_file(tmp_path):
    raw = synth.gesture('still', 1)
    p = synth.write_session(tmp_path / 's', raw, events=[{'t': 0.5, 'name': 'b'}, {'t': 0.1, 'name': 'a'}],
                            state={'t': [0, 0.02, 0.04], 'pitch': [1, 2, 3], 'flags': [1024, 0, 1024 | 1]})
    fb = bytes(range(256)) * 4
    (p / 'screen.bin').write_bytes(struct.pack('<d', 0.25) + fb + struct.pack('<d', 0.5) + fb)
    s = A.load_session(p)
    assert len(s.motion['t']) == 500 and s.raw.shape == (500, 6) and s.rate_hz == 500
    assert [e['name'] for e in s.events] == ['a', 'b']
    assert s.col('rest').tolist() == [True, False, True] and s.col('frozen').tolist() == [False, False, True]
    assert s.col('speed') is None and s.col('pitch').tolist() == [1, 2, 3]
    assert s.screen.shape == (2, 1024) and s.screen_t.tolist() == [0.25, 0.5] and s.screen[0, 255] == 255
    assert s.axis_map == (3, 1, 5) and s.lsb == (16384.0, 32.8)


def test_load_session_tolerates_missing_files(tmp_path):
    (tmp_path / 'e').mkdir()
    s = A.load_session(tmp_path / 'e')
    assert len(s.motion['t']) == 0 and s.raw is None and s.state == {} and s.events == []
    assert A.rest_stats(s)['findings'][0]['metric'] == 'samples'
    assert A.drift(s)['metrics'] == {}
    with pytest.raises(FileNotFoundError):
        A.load_session(tmp_path / 'nothing')


def test_lsb_from_the_range_sizes(tmp_path):
    (tmp_path / 's').mkdir()
    (tmp_path / 's' / 'meta.json').write_text(json.dumps({'imu': {'acc_range_g': 8, 'gyro_range_dps': 2000}}))
    assert A.load_session(tmp_path / 's').lsb == (4096.0, 16.4)


def test_text_columns_stay_text(tmp_path):
    (tmp_path / 's').mkdir()
    (tmp_path / 's' / 'state.csv').write_text('t,state,pitch\n0,idle,1.5\n0.02,play,2.5\n')
    s = A.load_session(tmp_path / 's')
    assert s.state['state'].tolist() == ['idle', 'play'] and s.state['pitch'].tolist() == [1.5, 2.5]


# ---------------------------------------------------------------- lying still
def test_rest_stats_recovers_bias_offset_noise(still):
    r = A.rest_stats(still, flat=True)
    json.dumps(r)
    m = r['metrics']
    assert m['still'] is True
    assert np.allclose(m['gyro_bias_chip'], CHIP_BIAS, atol=0.05)
    assert np.allclose(m['gyro_bias'], [0.6, -0.9, -0.4], atol=0.05)
    assert np.allclose(m['acc_offset_chip'], CHIP_OFF, atol=0.002)
    assert np.allclose(m['gyro_std'], 0.06, rtol=0.2)
    assert m['pitch_acc_noise'] == pytest.approx(np.degrees(0.004), rel=0.2)
    assert m['allan']['x']['0.1s'] == pytest.approx(0.06 / np.sqrt(50), rel=0.3)
    assert m['allan']['x']['10s'] is None
    assert m['saturated'] == 0
    s = r['suggest']
    assert np.allclose([s[f'imu.gyro_bias_{a}'] for a in 'xyz'], CHIP_BIAS, atol=0.05)
    assert np.allclose([s[f'imu.acc_off_{a}'] for a in 'xyz'], CHIP_OFF, atol=0.002)
    assert 'bias_learned_chip' in m                    # the board's own learning, from state.csv


def test_rest_stats_other_bias_and_no_offset_unless_flat(tmp_path, twin):
    raw = synth.gesture('still', 6, gyro_bias=(-2.0, 1.5, 0.3), seed=4)
    s = session(tmp_path / 'b', raw)
    r = A.rest_stats(s)
    assert np.allclose(r['metrics']['gyro_bias_chip'], [-2.0, 1.5, 0.3], atol=0.05)
    assert 'imu.acc_off_x' not in r['suggest'] and r['suggest']['imu.gyro_bias_x'] == pytest.approx(-2.0, abs=0.05)


def test_rest_stats_on_a_wrongly_mapped_session_still_finds_the_chip_bias(tmp_path):
    raw = synth.gesture('still', 4)
    s = session(tmp_path / 'w', raw, axis_map=(2, 1, 4), with_state=False)
    assert np.allclose(A.rest_stats(s)['metrics']['gyro_bias_chip'], CHIP_BIAS, atol=0.05)


def test_rest_stats_says_nothing_when_moving(tmp_path):
    s = session(tmp_path / 'm', synth.gesture('free', 8), with_state=False)
    r = A.rest_stats(s, flat=True)
    assert r['metrics']['still'] is False and r['suggest'] == {}


def test_allan_of_white_noise():
    rng = np.random.default_rng(0)
    x = 0.1 * rng.standard_normal(500 * 60)
    ad = A.allan(x, 500, (0.1, 1.0, 10.0, 30.0))
    for tau in (0.1, 1.0, 10.0):
        assert ad[tau] == pytest.approx(0.1 / np.sqrt(500 * tau), rel=0.3)
    assert ad[30.0] is None


def test_saturation_counts(tmp_path):
    raw = synth.gesture('flicks', 6, n=5, amp=40, width=0.12, gyro_range=0)
    s = session(tmp_path / 'f', raw, gyro_range=0, with_state=False)
    sat = A.saturation(s)
    assert sat['total'] > 0 and 'gyro y' in sat['by_axis'] and '250 deg/s' in sat['note']
    q = A.stream_quality(s)
    assert q['metrics']['saturated'] == sat['total'] and q['metrics']['motion_hz'] == 500


def test_stream_quality_sees_gaps_and_overflow(tmp_path):
    raw = synth.gesture('still', 2)
    fl = np.full(len(raw), 4)
    fl[300] |= 2
    p = synth.write_session(tmp_path / 'g', raw, flags=fl)
    lines = (p / 'motion.csv').read_text().splitlines()
    (p / 'motion.csv').write_text('\n'.join(lines[:200] + lines[260:]) + '\n')
    (p / 'raw.txt').unlink()
    q = A.stream_quality(A.load_session(p))
    assert q['metrics']['motion_gaps'] == 1 and q['metrics']['overflow'] == 1


def test_drift_slope():
    t = np.arange(0, 60, 0.02)
    rng = np.random.default_rng(2)
    st = {'t': t, 'yaw': 3.0 / 60 * t + 0.05 * rng.standard_normal(len(t)), 'pitch': 0.1 + 0 * t,
          'roll': -0.5 / 60 * t, 'flags': np.full(len(t), 1024 | 64)}
    s = A.Session(path=__import__('pathlib').Path('.'), meta={}, motion={k: np.zeros(0) for k in
                  ('t', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'flags')}, raw=None, state=st)
    d = A.drift(s)['metrics']
    assert d['yaw_drift'] == pytest.approx(3.0, abs=0.05) and d['roll_drift'] == pytest.approx(-0.5, abs=0.05)
    assert d['rest_only'] is True


def test_drift_wraps_yaw():
    t = np.arange(0, 120, 0.02)
    yaw = (170 + 10.0 / 60 * t + 180) % 360 - 180                # crosses +180 to -180
    s = A.Session(path=__import__('pathlib').Path('.'), meta={}, motion={k: np.zeros(0) for k in
                  ('t', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'flags')}, raw=None, state={'t': t, 'yaw': yaw})
    assert A.drift(s)['metrics']['yaw_drift'] == pytest.approx(10.0, abs=0.05)


# ---------------------------------------------------------------- axes
def axes_session(tmp_path, name, mount=synth.MOUNT, axis_map=synth.MOUNT, keep_raw=True):
    raw, tr = synth.gesture('axes', 16, mount=mount, with_truth=True)
    p = synth.write_session(tmp_path / name, raw, axis_map=axis_map)
    if not keep_raw:
        (p / 'raw.txt').unlink()
    segs = [{'label': g['label'], 't0': g['t0'] + 0.3, 't1': g['t1']} for g in tr['segments']]
    return A.load_session(p), segs


def test_axis_check_right_map(tmp_path):
    s, segs = axes_session(tmp_path, 'a')
    r = A.axis_check(s, segs)
    json.dumps(r)
    assert r['axis_map'] == [3, 1, 5] and r['current_ok'] is True and r['suggest'] == {}
    assert r['determined'] == [True, True, True]


@pytest.mark.parametrize('wrong', [(3, 0, 4), (2, 1, 4), (0, 3, 4), (3, 1, 4), (0, 2, 4)])
def test_axis_check_wrong_map_in_the_session(tmp_path, wrong):
    s, segs = axes_session(tmp_path, 'w', axis_map=wrong)
    r = A.axis_check(s, segs)
    assert r['axis_map'] == [3, 1, 5] and r['current_ok'] is False
    assert r['suggest'] == {'motion.ax_x': 3, 'motion.ax_y': 1, 'motion.ax_z': 5}


@pytest.mark.parametrize('mount', [(2, 1, 4), (3, 0, 4), (1, 3, 4), (0, 2, 4), (3, 1, 5)])
def test_axis_check_finds_other_mountings(tmp_path, mount):
    s, segs = axes_session(tmp_path, 'm', mount=mount)
    r = A.axis_check(s, segs)
    assert tuple(r['axis_map']) == mount and r['current_ok'] is (mount == synth.MOUNT)
    if mount != synth.MOUNT:
        assert r['suggest'] == {f'motion.ax_{a}': mount[k] for k, a in enumerate('xyz')}


def test_axis_check_without_raw_uses_the_sessions_map(tmp_path):
    s, segs = axes_session(tmp_path, 'n', keep_raw=False)
    assert A.axis_check(s, segs)['axis_map'] == [3, 1, 5]


def test_axis_check_flat_only_knows_z(tmp_path):
    p = synth.write_session(tmp_path / 'f', synth.gesture('still', 3), axis_map=(2, 0, 5))   # x and y wrong, z right
    r = A.axis_check(A.load_session(p), [('flat', 0.5, 3)])
    assert r['determined'] == [False, False, True] and r['current_ok'] is True and r['suggest'] == {}
    assert r['axis_map'][2] == 5
    notes = ' '.join(f['note'] for f in r['findings'])
    assert 'Not checked' in notes


def test_axis_check_flat_only_screen_down_mount(tmp_path):
    # the chip turned over: z is wrong, but one axis is too little to change the map
    p = synth.write_session(tmp_path / 'u', synth.gesture('still', 3, mount=(3, 0, 4)))
    r = A.axis_check(A.load_session(p), [('flat', 0.5, 3)])
    assert r['axis_map'][2] == 4 and r['current_ok'] is False and r['suggest'] == {}


def test_axis_check_two_axes_fix_the_third(tmp_path):
    raw, tr = synth.gesture('axes', 16, mount=(2, 1, 4), with_truth=True)
    p = synth.write_session(tmp_path / 't', raw)
    segs = [{'label': g['label'], 't0': g['t0'] + 0.3, 't1': g['t1']} for g in tr['segments'] if g['label'] in
            ('flat', 'nose_down')]
    r = A.axis_check(A.load_session(p), segs)
    assert r['determined'] == [True, True, True] and r['axis_map'] == [2, 1, 4]


# ---------------------------------------------------------------- holds and responses
STEPS = [(0, 0, 3), (15, 0, 3), (30, 0, 3), (45, 0, 3), (0, 0, 3), (-30, 0, 3), (0, -35, 3), (0, 25, 3)]


def test_holds_recover_the_angles(tmp_path, twin):
    raw, tr = synth.gesture('steps', 24, steps=STEPS, with_truth=True)
    s = session(tmp_path / 'h', raw, tune=OFF_TUNE)
    segs = [{'label': g['label'], 't0': g['t0'] - 0.3, 't1': g['t1'], 'pitch': g['pitch'], 'roll': g['roll']}
            for g in tr['segments']]
    r = A.holds(s, segs)
    json.dumps(r)
    rows = r['metrics']['steps']
    assert len(rows) == len(STEPS)
    for row, (p, rr, _) in zip(rows, STEPS):
        assert row['pitch'] == pytest.approx(p, abs=0.5) and row['roll'] == pytest.approx(rr, abs=0.5)
        assert row['pitch_acc'] == pytest.approx(p, abs=0.5) and row['roll_acc'] == pytest.approx(rr, abs=0.5)
        assert abs(row['pitch_vs_acc']) < 0.2 and row['pitch_std'] < 0.3
        assert row['zone'] == ('dead' if abs(p) < 5 else 'fwd' if p > 0 else 'back')
        assert row['settle_s'] is not None and row['settle_s'] < 1.0
    assert rows[3]['speed'] == pytest.approx(2.0, rel=0.03)      # Speed max at tilt.full
    assert rows[0]['speed'] == pytest.approx(0.05)                # Speed min in the dead zone


def test_holds_without_the_offset_in_the_tune_read_the_offsets_error(tmp_path, twin):
    # nose down 30: gravity to -y; the offset's y and z (device -0.010, -0.020) then pull the angle the same way
    raw, tr = synth.gesture('steps', 6, steps=[(0, 0, 3), (30, 0, 3)], with_truth=True)
    s = session(tmp_path / 'o', raw)
    row = A.holds(s, [{'label': 'down', 't0': 3.3, 't1': 6, 'pitch': 30}])['metrics']['steps'][0]
    a = np.array([0, -0.5, 0.866]) + synth.remap(CHIP_OFF)
    want = np.degrees(np.arctan2(-a[1], np.hypot(a[0], a[2])))      # what the board reads without the offset
    assert row['pitch'] == pytest.approx(want, abs=0.2) and abs(row['pitch_err']) > 0.8


def test_step_response_first_order():
    t = np.arange(0, 2, 0.001)
    tau, d = 0.1, 0.05
    x = np.where(t < 1 + d, 0.0, 10 * (1 - np.exp(-(t - 1 - d) / tau)))
    m = A.step_response(t, x, 1.0, 10.0)['metrics']
    assert m['delay_63_ms'] == pytest.approx(150, abs=2)
    assert m['delay_10_ms'] == pytest.approx(50 + 1000 * tau * np.log(1 / 0.9), abs=2)
    assert m['delay_90_ms'] == pytest.approx(50 + 1000 * tau * np.log(10), abs=2)
    assert m['overshoot_pct'] == 0 and m['settle_ms'] == pytest.approx(50 + 1000 * tau * np.log(20), abs=3)


def test_step_response_overshoot_and_window():
    t = np.arange(0, 3, 0.001)
    z, wn = 0.5, 20.0
    wd = wn * np.sqrt(1 - z * z)
    u = np.clip(t - 1, 0, None)
    y = 1 - np.exp(-z * wn * u) * (np.cos(wd * u) + z / np.sqrt(1 - z * z) * np.sin(wd * u))
    y[t > 2.0] = 5                                                 # the next step, cut off by t_end
    m = A.step_response(t, y, 1.0, 1.0, t_end=2.0)['metrics']
    assert m['overshoot_pct'] == pytest.approx(100 * np.exp(-np.pi * z / np.sqrt(1 - z * z)), abs=0.5)
    assert A.step_response(t, y, 1.0, 1.0)['metrics']['settle_ms'] is None


def state_session(st, events=None, tune=None):
    return A.Session(path=__import__('pathlib').Path('.'), meta={'tune': tune or {}},
                     motion={k: np.zeros(0) for k in ('t', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'flags')},
                     raw=None, state={k: np.asarray(v) for k, v in st.items()}, events=events or [])


def test_jitter_in_cents():
    rng = np.random.default_rng(3)
    t = np.arange(0, 20, 0.02)
    sp = 1.5 * 2 ** (5.0 * rng.standard_normal(len(t)) / 1200)
    m = A.jitter(state_session({'t': t, 'speed_cmd': sp, 'pitch': 20 + 0.1 * rng.standard_normal(len(t))}))['metrics']
    assert m['speed_cmd_cents_std'] == pytest.approx(5.0, rel=0.1) and m['pitch_std'] == pytest.approx(0.1, rel=0.15)
    r = A.jitter(state_session({'t': t, 'speed_cmd': 0.05 + 0 * t}))
    assert r['metrics']['speed_cmd_std'] == 0 and 'dead zone' in r['findings'][0]['note']


def test_shake_is_misread_by_acc_and_held_by_comp(tmp_path, twin):
    raw, tr = synth.gesture('shake', 6, amp=0.5, start=2.0, end=5.5, with_truth=True)
    shake, rest = (2.5, 5.0), (0.5, 1.9)
    acc = A.shake_rejection(session(tmp_path / 'acc', raw), shake, rest)
    json.dumps(acc)
    m = acc['metrics']
    assert m['shake']['pitch_p2p'] > 5 and m['shake']['gyro_pitch_p2p'] < 0.5 and m['rest']['pitch_p2p'] < 1
    assert acc['suggest'] == {'motion.fusion': 1}
    comp = A.shake_rejection(session(tmp_path / 'comp', raw, tune={'motion.fusion': 1}), shake, rest)
    assert comp['metrics']['shake']['pitch_p2p'] < acc['metrics']['shake']['pitch_p2p'] / 3
    assert comp['suggest'] == {}


# ---------------------------------------------------------------- the encoder, REC and the joystick
def enc_events(turns=(), presses=(), tune=None):
    """What gesture.c and main.c report for these turns [(t, detents counted)] and presses [(t_down, ms)], by its rules."""
    tv = {**{k: A.tune_default(k) for k in ('enc.tap_min', 'enc.hold', 'enc.long', 'enc.double')}, **(tune or {})}
    ev = [{'t': t, 'name': 'enc.turn', 'v': v, 'ms': 0} for t, v in turns]
    last_tap = None
    for t, ms in presses:
        ev.append({'t': t, 'name': 'enc.down', 'v': 0, 'ms': 0})
        up = t + ms / 1000
        if ms >= tv['enc.long']:
            ev.append({'t': t + tv['enc.long'] / 1000, 'name': 'enc.long', 'v': 0, 'ms': int(tv['enc.long'])})
        ev.append({'t': up, 'name': 'enc.up', 'v': 0, 'ms': ms})
        if ms >= tv['enc.long']:
            continue
        cls = 'bounce' if ms < tv['enc.tap_min'] else 'tap' if ms < tv['enc.hold'] else 'hold'
        ev.append({'t': up, 'name': f'enc.{cls}', 'v': 0, 'ms': ms})
        if cls == 'tap':
            if last_tap is not None and (up - last_tap) * 1000 < tv['enc.double']:
                ev.append({'t': up, 'name': 'enc.double', 'v': 0, 'ms': 0})
                last_tap = None
            else:
                last_tap = up
    return sorted(ev, key=lambda e: e['t'])


def turns(n, d, t0, gap=0.3):
    return [(t0 + gap * i, d) for i in range(n)]


def test_encoder_reversed():
    ev = enc_events(turns(10, -1, 1.0) + turns(10, 1, 5.0))
    r = A.encoder_stats(ev, {'right': 10, 'left': 10})
    json.dumps(r)
    assert r['suggest'] == {'enc.dir': 1}
    assert any('asked for 10 right, counted -10: flip enc.dir' in f['note'] for f in r['findings'])


def test_encoder_reversed_with_dir_already_plus():
    ev = enc_events(turns(10, -1, 1.0))
    assert A.encoder_stats(ev, {'right': 10}, tune={'enc.dir': 1})['suggest'] == {'enc.dir': 0}


def test_encoder_double_counting():
    ev = enc_events(turns(20, 1, 1.0, 0.15) + turns(20, -1, 5.0, 0.15))
    r = A.encoder_stats(ev, {'right': 10, 'left': 10})
    assert r['suggest'] == {'enc.steps': 8}
    assert any('asked 20 detents, counted 40: enc.steps 8' in f['note'] for f in r['findings'])


def test_encoder_half_counting_with_windows():
    ev = enc_events(turns(5, 1, 1.0) + turns(5, -1, 5.0))
    r = A.encoder_stats(ev, {'right': 10, 'left': 10, 'windows': {'right': (0.5, 4), 'left': (4.5, 8)}})
    assert r['suggest'] == {'enc.steps': 2} and r['metrics']['counted_right'] == 5


def test_encoder_right_says_so():
    ev = enc_events(turns(10, 1, 1.0) + turns(9, -1, 5.0))      # one detent short: the hand, not the setting
    r = A.encoder_stats(ev, {'right': 10, 'left': 10})
    assert r['suggest'] == {}
    assert any(f['value'] == 'right' for f in r['findings'] if f['metric'] == 'direction')


def test_encoder_short_taps_lower_tap_min():
    ev = enc_events(presses=[(1 + i, ms) for i, ms in enumerate((35, 60, 80, 50, 45))])
    r = A.encoder_stats(ev, {'taps': 5})
    assert r['metrics']['bounces'] == 1 and r['suggest'] == {'enc.tap_min': 25}
    assert any('the shortest tap 35 ms: under enc.tap_min 40, lower it to 25' in f['note'] for f in r['findings'])


def test_encoder_contact_bounce_is_not_a_tap():
    ev = enc_events(presses=[(1, 3), (2, 120), (3, 4), (4, 150)])
    r = A.encoder_stats(ev, {'taps': 2})
    assert r['suggest'] == {} and any(f['metric'] == 'contact bounce' for f in r['findings'])


def test_encoder_long_taps_raise_hold():
    taps = [(1 + i, ms) for i, ms in enumerate((180, 300, 560, 420, 250))]
    holds = [(8 + 2 * i, ms) for i, ms in enumerate((900, 1100, 950))]
    ev = enc_events(presses=taps + holds)
    win = {'taps': (0.5, 7), 'holds': (7.5, 14)}
    r = A.encoder_stats(ev, {'taps': 5, 'holds': 3, 'windows': win})
    assert r['suggest'] == {'enc.hold': 730}
    assert r['metrics']['longest_tap_ms'] == 560 and r['metrics']['shortest_hold_ms'] == 900
    # the same without windows: sorted by length, the five shortest are the taps
    assert A.encoder_stats(ev, {'taps': 5, 'holds': 3})['suggest'] == {'enc.hold': 730}


def test_encoder_short_holds_lower_hold():
    ev = enc_events(presses=[(1, 120), (2, 150), (3, 400), (5, 420)])
    r = A.encoder_stats(ev, {'taps': 2, 'holds': 2})
    assert r['suggest'] == {'enc.hold': 280}


def test_encoder_slow_double_taps_raise_double():
    ev = enc_events(presses=[(1.0, 100), (1.35, 100), (4.0, 100), (4.33, 100)])
    r = A.encoder_stats(ev, {'doubles': 2})
    assert r['metrics']['doubles'] == 0 and r['suggest'] == {'enc.double': 460}
    ok = enc_events(presses=[(1.0, 100), (1.2, 100), (4.0, 100), (4.22, 100)])
    assert A.encoder_stats(ok, {'doubles': 2})['metrics']['doubles'] == 2


def test_encoder_reads_the_board_events_not_the_table():
    # the board said tap at 45 ms (its enc.tap_min was 30 then): the events win over today's table
    ev = enc_events(presses=[(1, 45)], tune={'enc.tap_min': 30})
    assert A.encoder_stats(ev)['metrics']['taps'] == 1


def test_rec_stats():
    ev = []
    for i, ms in enumerate((150, 160, 140, 155, 25, 170, 2000, 2100)):
        ev += [{'t': 1 + 3 * i, 'name': 'rec.down'}, {'t': 1 + 3 * i + ms / 1000, 'name': 'rec.up', 'ms': 0}]
    r = A.rec_stats(ev, {'short': 5, 'long': 2})
    json.dumps(r)
    m = r['metrics']
    assert m['presses'] == 8 and m['bounces'] == 1 and m['longest_ms'] == 2100 and m['shortest_ms'] == 25
    assert r['suggest'] == {'rec.debounce': 4}
    assert m['long_ms'] == [2000, 2100]


def test_rec_stats_missing_presses():
    ev = [{'t': 1, 'name': 'rec.down'}, {'t': 1.2, 'name': 'rec.up'}, {'t': 3, 'name': 'rec.down'}]
    r = A.rec_stats(ev, {'short': 5})
    assert r['metrics']['presses'] == 1 and r['metrics']['unreleased'] is True
    assert any(f['metric'] == 'missed' for f in r['findings'])


def shape(raw, centre, dead=0.12):
    """gesture.c axis(): raw ADC to -1..1 past the dead zone (for the test's own state columns)."""
    v = np.where(raw > centre, (raw - centre) / (4095 - centre), (raw - centre) / centre)
    a = np.abs(v)
    return np.where(a < dead, 0.0, np.clip((a - dead) / (1 - dead) * np.sign(v), -1, 1))


def joy_session(noise, reach_ym=0.6, centre=(1980, 2110)):
    rng = np.random.default_rng(5)
    cx, cy = centre
    t = np.arange(0, 12, 0.02)
    rest = t < 3
    ang = 2 * np.pi * (t - 3) / 4.5
    nx, ny = 0.95 * np.cos(ang), 0.95 * np.sin(ang)
    ny = np.where(ny < 0, ny * reach_ym / 0.95, ny)
    rx = np.where(rest, cx + noise * rng.standard_normal(len(t)), np.where(nx > 0, cx + nx * (4095 - cx), cx + nx * cx))
    ry = np.where(rest, cy + noise * rng.standard_normal(len(t)), np.where(ny > 0, cy + ny * (4095 - cy), cy + ny * cy))
    ax, ay = shape(rx, cx), shape(ry, cy)
    jx, jy = ay, -ax                                              # joy.rotate 90
    ev = [{'t': 5, 'name': 'joy.flick', 'v': 0.8, 'text': 'right'}, {'t': 6, 'name': 'joy.flick', 'v': -0.8, 'text': 'up'}]
    return state_session({'t': t, 'joy_rx': rx, 'joy_ry': ry, 'joy_x': jx, 'joy_y': jy}, ev)


def test_joystick_centre_reach_and_flicks():
    r = A.joystick_stats(joy_session(noise=6), {'flicks': 4})
    json.dumps(r)
    m = r['metrics']
    assert m['centre'] == pytest.approx([1980, 2110], abs=3)
    assert m['reach']['raw x +'] == pytest.approx(0.95, abs=0.02) and m['reach']['raw y -'] == pytest.approx(0.6, abs=0.02)
    assert m['flicks'] == {'left': 0, 'right': 1, 'up': 1, 'down': 0}
    assert 'joy.dead' not in r['suggest']                        # a quiet stick: 0.12 clears it
    # raw y down reaches 0.6: shaped, the left side tops at (0.6 - 0.12) / 0.88 = 0.55, under joy.flick 0.7
    assert m['shaped_reach']['left'] == pytest.approx(0.545, abs=0.02)
    assert r['suggest'].get('joy.flick') == pytest.approx(0.45)


def test_joystick_noisy_centre_wants_a_wider_dead_zone():
    r = A.joystick_stats(joy_session(noise=110))
    assert r['suggest']['joy.dead'] > 0.12


def test_joystick_absent():
    r = A.joystick_stats(state_session({'t': [0, 1], 'joy_rx': [0, 0], 'joy_ry': [0, 0]}))
    assert 'No joystick' in r['findings'][0]['note'] and r['suggest'] == {}


# ---------------------------------------------------------------- compare
def test_compare_rows():
    a = {'metrics': {'gyro_std': 0.06, 'lag_ms': 40, 'count': 5, 'nested': {'drift': -2.0}, 'flag': True}}
    b = {'metrics': {'gyro_std': 0.08, 'lag_ms': 40.4, 'count': 6, 'nested': {'drift': 1.0}, 'extra_noise': 1}}
    rows = {r['metric']: r for r in A.compare(a, b)}
    assert rows['gyro_std']['better'] == 'A' and rows['lag_ms']['better'] == 'same'
    assert rows['count']['better'] == '' and rows['nested.drift']['better'] == 'B'
    assert rows['extra_noise']['A'] is None and 'flag' not in rows
    # the roll's delay is an amount, not a lag: no rule for it; a step's delay is
    rows = {r['metric']: r for r in A.compare({'left': {'delay': 0.5}, 'delay_63_ms': 40},
                                              {'left': {'delay': 0.7}, 'delay_63_ms': 60})}
    assert rows['left.delay']['better'] == '' and rows['delay_63_ms']['better'] == 'A'
    json.dumps(A.compare(a, b))


def test_everything_is_json(still):
    for r in (A.rest_stats(still), A.drift(still), A.stream_quality(still), A.jitter(still, 2, 8),
              A.axis_check(still, [('flat', 1, 9)]), A.holds(still, [('flat', 1, 9)])):
        json.dumps(r, allow_nan=False)
