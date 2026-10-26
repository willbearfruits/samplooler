"""synth.py: the twin's motion model in numpy. Known poses must read as known gravity and rates in the right axes."""
import itertools
import json
import subprocess

import numpy as np
import pytest

from slrig import synth

DEV_OFF = np.array([0.006, 0.010, 0.020])     # the twin's chip offset (0.010, -0.006, 0.020) in device axes
DEV_BIAS = np.array([0.6, 0.9, 0.4])          # the twin's chip bias (0.9, -0.6, 0.4) in device axes


@pytest.fixture(scope='module')
def twin():
    if not synth.TWIN.exists():
        subprocess.run(['sh', str(synth.RIG / 'twin' / 'build.sh')], capture_output=True)
    if not synth.TWIN.exists():
        pytest.skip('the twin is not built (rig/twin/build.sh)')
    return synth.TWIN


def all_maps():
    return [m for m in itertools.product(range(6), repeat=3) if len({e >> 1 for e in m}) == 3]


def test_still_reads_offset_bias_and_noise():
    raw = synth.gesture('still', 10)
    assert raw.dtype == np.int16 and raw.shape == (5000, 6)
    d = synth.to_device(raw)
    assert np.allclose(d[:, :3].mean(0), DEV_OFF + [0, 0, 1], atol=0.0005)
    assert np.allclose(d[:, 3:].mean(0), DEV_BIAS, atol=0.01)
    assert np.allclose(d[:, :3].std(0), 0.004, rtol=0.1)
    assert np.allclose(d[:, 3:].std(0), 0.06, rtol=0.1)


@pytest.mark.parametrize('pitch,roll', [(30, 0), (-45, 0), (0, 40), (0, -20), (20, 25)])
def test_tilt_reads_gravity(pitch, roll):
    raw, tr = synth.gesture('tilt', 3, pitch=pitch, roll=roll, noise_g=0, acc_offset=(0, 0, 0), with_truth=True)
    a = synth.to_device(raw)[-200:, :3].mean(0)
    th, ph = np.radians(pitch), np.radians(roll)
    up = np.array([-np.sin(ph) * np.cos(th), np.sin(th), np.cos(th) * np.cos(ph)])
    assert np.allclose(a / np.linalg.norm(a), up / np.linalg.norm(up), atol=0.002)
    assert tr['pitch'][-1] == pytest.approx(pitch) and tr['roll'][-1] == pytest.approx(roll)
    # the angles back from gravity, by motion.c's ACC formula
    ax, ay, az = a
    p, r = np.radians(pitch), np.radians(roll)
    acc_roll = np.degrees(np.arctan2(np.sin(r) * np.cos(p), np.sqrt(np.sin(p) ** 2 + (np.cos(p) * np.cos(r)) ** 2)))
    assert np.degrees(np.arctan2(ay, np.hypot(ax, az))) == pytest.approx(pitch, abs=0.15)
    assert np.degrees(np.arctan2(-ax, np.hypot(ay, az))) == pytest.approx(acc_roll, abs=0.15)
    if pitch == 0:
        assert acc_roll == pytest.approx(roll)


def test_mount_is_the_twins():
    # twin.c: chip x = device y, chip y = -device x, chip z = device z
    raw = synth.gesture('tilt', 3, roll=30, noise_g=0, acc_offset=(0, 0, 0))
    chip = raw[-100:, :3].mean(0) / 16384
    assert chip[1] == pytest.approx(0.5, abs=0.002)          # right edge down: device x = -0.5, chip y = +0.5
    assert synth.remap([1, 2, 3], (3, 0, 4)).tolist() == [-2, 1, 3]
    assert synth.unmap([-2, 1, 3], (3, 0, 4)).tolist() == [1, 2, 3]


@pytest.mark.parametrize('m', all_maps())
def test_maps_round_trip(m):
    rng = np.random.default_rng(1)
    raw = rng.integers(-30000, 30000, size=(50, 6)).astype(np.int16)
    dev = synth.to_device(raw, 8192, 65.5, m)
    assert np.array_equal(synth.to_raw(dev, 8192, 65.5, m), raw)


def test_unmap_refuses_a_map_with_a_repeated_axis():
    with pytest.raises(ValueError):
        synth.unmap([1, 2, 3], (0, 0, 4))
    with pytest.raises(ValueError):
        synth.remap([1, 2, 3], (0, 7, 4))


def test_sweep_body_rate():
    raw = synth.gesture('sweep', 9, a=-45, b=45)               # 90 deg over 8 s
    g = synth.to_device(raw)[1000:3500, 3:].mean(0)
    assert g[0] == pytest.approx(90 / 8 + DEV_BIAS[0], abs=0.05)
    assert g[1] == pytest.approx(DEV_BIAS[1], abs=0.05)


def test_flick_peak_rate_and_saturation():
    raw, tr = synth.gesture('flicks', 6, n=5, amp=30, width=0.15, with_truth=True)
    gx = synth.to_device(raw)[:, 3]
    assert gx.max() == pytest.approx(np.pi * 30 / 0.15, rel=0.01)
    assert len(tr['segments']) == 5
    small = synth.gesture('flicks', 6, n=5, amp=40, width=0.12, gyro_range=0)
    assert (np.abs(small[:, 3:]) >= 32767).any()


def test_shake_moves_gravity_not_the_gyro():
    raw, tr = synth.gesture('shake', 4, amp=0.5, with_truth=True)
    d = synth.to_device(raw)
    seg = tr['segments'][1]
    k = (tr['t'] > seg['t0']) & (tr['t'] < seg['t1'])
    assert np.abs(d[k, :3] - d[:200, :3].mean(0)).max() > 0.4
    assert np.allclose(d[k, 3:].std(0), 0.06, rtol=0.15)


def test_axes_holds():
    raw, tr = synth.gesture('axes', 16, with_truth=True)
    d = synth.to_device(raw)
    want = {'flat': (0, 0, 1), 'nose_down': (0, 0.866, 0.5), 'right_down': (-0.866, 0, 0.5), 'facing': (0, -1, 0)}
    for s in tr['segments']:
        k = (tr['t'] > s['t0'] + 0.2) & (tr['t'] < s['t1'])
        assert np.allclose(d[k, :3].mean(0) - DEV_OFF, want[s['label']], atol=0.01), s['label']


def test_steps_segments_and_ramps():
    tr = synth.pose_series('steps', 9, 500, steps=[(10, 0, 3), (20, 5, 3), (-10, 0, 3, 'up')], ramp=0.3)
    labels = [s['label'] for s in tr['segments']]
    assert labels == ['pitch 10 roll 0', 'pitch 20 roll 5', 'up']
    for s in tr['segments']:
        k = (tr['t'] >= s['t0']) & (tr['t'] < s['t1'])
        assert np.allclose(tr['pitch'][k], s['pitch']) and np.allclose(tr['roll'][k], s['roll'])
    assert np.abs(np.diff(tr['pitch'])).max() < 0.5             # smooth ramps, no jumps


def test_tilt_from_a_previous_pose_is_continuous():
    tr = synth.pose_series('tilt', 3, 500, pitch=30, from_pitch=15, start=0.3)
    assert tr['pitch'][0] == pytest.approx(15) and tr['pitch'][-1] == pytest.approx(30)


def test_pose_series_is_the_gestures_truth():
    raw, tr = synth.gesture('free', 6, seed=3, with_truth=True)
    ps = synth.pose_series('free', 6, 500, seed=3)
    for k in ('t', 'pitch', 'roll', 'yaw'):
        assert np.array_equal(tr[k], ps[k])
    assert synth.gesture_truth is synth.pose_series


def test_free_is_seeded():
    a, b, c = synth.gesture('free', 4, seed=1), synth.gesture('free', 4, seed=1), synth.gesture('free', 4, seed=2)
    assert np.array_equal(a, b) and not np.array_equal(a, c)


def test_bad_kind_and_parameter():
    with pytest.raises(ValueError):
        synth.gesture('wobble', 1)
    with pytest.raises(ValueError):
        synth.gesture('tilt', 1, pich=30)


def test_upside_down_has_no_nan():
    q = synth.pose_q(np.array([0.0, 180.0]), np.array([180.0, 0.0]), np.zeros(2))
    assert np.isfinite(q).all()


def test_write_session(tmp_path):
    raw = synth.gesture('tilt', 2, pitch=20)
    p = synth.write_session(tmp_path / 's', raw, events=[{'t': 1.0, 'name': 'enc.tap', 'ms': 80}],
                            state={'t': [0, 0.02], 'pitch': [1, 2]}, test={'name': 'x', 'steps': []})
    meta = json.loads((p / 'meta.json').read_text())
    assert meta['imu']['axis_map'] == [3, 0, 4] and meta['imu']['rate_hz'] == 500
    assert meta['tune']['motion.ax_x'] == 3
    assert len((p / 'motion.csv').read_text().splitlines()) == 1001
    assert len((p / 'raw.txt').read_text().splitlines()) == 1000
    assert (p / 'state.csv').read_text().splitlines()[0] == 't,pitch'
    assert json.loads((p / 'events.jsonl').read_text())['name'] == 'enc.tap'


def test_twin_states_and_curve(twin):
    st = synth.twin_states(synth.gesture('tilt', 4, pitch=30))
    assert abs(len(st['t']) - 200) <= 1                        # 50 Hz ticks over 4 s (the first lands on sample 0)
    assert np.median(np.diff(st['t'])) == pytest.approx(0.02, abs=1e-3)
    assert st['pitch'][-1] == pytest.approx(29.92, abs=0.4)    # the offset's own error, as the board would read it
    assert st['speed_cmd'][-1] > 0.8 and st['zone'][-1] == 1
    st = synth.twin_states(synth.gesture('still', 3), tune={'imu.acc_off_x': 0.010, 'imu.acc_off_y': -0.006,
                                                            'imu.acc_off_z': 0.020})
    assert abs(st['pitch'][-20:].mean()) < 0.1 and (st['flags'][-1] & 1024)
    c = synth.twin_curve(n=41)
    assert len(c['tilt']['angle']) == 41 and c['tilt']['dead'] == 5
    with pytest.raises(KeyError):
        synth.twin_states(synth.gesture('still', 1), tune={'tilt.nope': 1})
