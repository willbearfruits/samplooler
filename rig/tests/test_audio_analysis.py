"""audio_analysis.py on signals with known answers: levels of a known sine, clicks at known times, the pitch of a known
tone, the speed of a tone played through a pretend tape, and broken buffers."""
import json

import numpy as np
import pytest

from slrig import audio_analysis as au

SR = 32000


def sine(f=440.0, secs=2.0, amp=0.5, sr=SR):
    return amp * np.sin(2 * np.pi * f * np.arange(int(secs * sr)) / sr)


def tape(speed, secs=3.0, tone=440.0, sr=SR):
    """A recorded tone played at this speed (a number or one per sample): the read head's position is the speed's sum."""
    n = int(secs * sr)
    sp = np.full(n, float(speed)) if np.ndim(speed) == 0 else np.asarray(speed, float)
    return 0.4 * np.sin(2 * np.pi * tone * np.cumsum(sp) / sr)


def test_wav_round_trip(tmp_path):
    x = sine(secs=0.5)
    st = np.column_stack([x, -x])
    x16, sr = au.read_wav(au.write_wav(tmp_path / 'a.wav', x, SR))
    assert sr == SR and x16.shape == x.shape and np.abs(x16 - x).max() < 1 / 32768 + 1e-9
    s32, _ = au.read_wav(au.write_wav(tmp_path / 'b.wav', st, SR, bits=32))
    assert s32.shape == (len(x), 2) and np.abs(s32 - st).max() < 1e-6
    with pytest.raises(ValueError):
        (tmp_path / 'c.wav').write_bytes(b'nope' * 10)
        au.read_wav(tmp_path / 'c.wav')


def test_wav_24_bit(tmp_path):
    import struct
    v = np.array([0, 1 << 22, -(1 << 22), (1 << 23) - 1], dtype=np.int64)
    data = b''.join(int(x & 0xFFFFFF).to_bytes(3, 'little') for x in v)
    hdr = (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, SR, SR * 3, 3, 24)
           + b'data' + struct.pack('<I', len(data)))
    (tmp_path / 'd.wav').write_bytes(hdr + data)
    x, _ = au.read_wav(tmp_path / 'd.wav')
    assert x.tolist() == pytest.approx([0, 0.5, -0.5, 1 - 2 ** -23])


def test_levels_of_a_sine_with_silence_dc_and_clips():
    rng = np.random.default_rng(0)
    x = np.concatenate([sine(amp=0.5), 10 ** (-80 / 20) * rng.standard_normal(SR)])
    m = au.levels(x, SR)
    json.dumps(m)
    assert m['peak_dbfs'] == pytest.approx(-6.02, abs=0.05)
    assert m['noise_floor_dbfs'] == pytest.approx(-80, abs=1.0)
    assert m['rms_dbfs'] == pytest.approx(20 * np.log10(np.sqrt((0.125 * 2 + 1e-8) / 3)), abs=0.1)
    assert au.levels(sine(amp=0.5) + 0.01, SR)['dc'] == pytest.approx(0.01, abs=1e-4)
    m2 = au.levels(np.clip(sine(amp=1.2), -1, 1), SR)
    # a sine of amplitude 1.2 is at or past the clip level for 1 - 2 asin(0.999 / 1.2) / pi of the time
    assert m2['clipped'] == pytest.approx(len(sine()) * (1 - 2 * np.arcsin(0.999 / 1.2) / np.pi), rel=0.02)
    st = au.levels(np.column_stack([sine(amp=0.5), sine(amp=0.25)]), SR)
    assert [c['peak_dbfs'] for c in st['channels']] == pytest.approx([-6.02, -12.04], abs=0.05)


def test_clicks_found_at_their_times():
    rng = np.random.default_rng(1)
    t = np.arange(4 * SR) / SR
    ph = np.zeros_like(t)
    at = (0.5, 1.1, 1.7, 2.9)
    for k, tc in enumerate(at):
        ph[int(tc * SR):] += (np.pi / 2) * (1 if k % 2 == 0 else -1) * (1 + 0.3 * k)
    x = 0.5 * np.sin(2 * np.pi * 440 * t + ph) + 10 ** (-70 / 20) * rng.standard_normal(len(t))
    c = au.clicks(x, SR)
    json.dumps(c)
    assert len(c) == len(at)
    for got, want in zip(c, at):
        assert got['t'] == pytest.approx(want, abs=0.001)


def test_clicks_dropped_sample_and_small_step():
    x = sine(220, 2.0, 0.3)
    drop = np.delete(x, int(0.8 * SR))
    assert [round(c['t'], 3) for c in au.clicks(drop, SR)] == [0.8]
    q = sine(220, 2.0, 0.05)
    q[int(0.9 * SR):] += 0.01                                      # a -40 dB step under quiet music
    assert [round(c['t'], 3) for c in au.clicks(q, SR)] == [0.9]


def test_no_clicks_in_clean_music_or_noise():
    rng = np.random.default_rng(2)
    music = sine(110, 3, 0.3) + sine(330, 3, 0.2) + sine(1250, 3, 0.1) + 10 ** (-60 / 20) * rng.standard_normal(3 * SR)
    assert au.clicks(music, SR) == []
    assert au.clicks(0.1 * rng.standard_normal(10 * SR), SR) == []
    assert au.clicks(np.zeros(SR), SR) == []


def test_pitch_track_of_a_sine():
    t, f0, conf = au.pitch_track(sine(220, 3, 0.3), SR, 50, 2000)
    assert len(t) == len(f0) == len(conf) > 100
    assert np.nanmedian(f0) == pytest.approx(220, rel=1e-4) and np.nanmax(np.abs(f0 - 220)) / 220 < 1e-3
    assert np.median(conf) > 0.95


def test_pitch_track_is_quiet_on_silence():
    t, f0, conf = au.pitch_track(np.zeros(SR), SR)
    assert np.isnan(f0).all() and (conf == 0).all()


@pytest.mark.parametrize('speed', [1.0, 1.37, 0.62, 0.25, 2.0, 4.0])
def test_speed_from_tone_constant(speed):
    r = au.speed_from_tone(tape(speed), SR, 440)
    assert r['median'] == pytest.approx(speed, rel=0.005)
    assert r['voiced'] > 0.9 and r['iqr_cents'] < 5


def test_speed_from_tone_follows_a_ramp_and_backwards():
    sp = np.linspace(0.5, 2.0, 4 * SR)
    r = au.speed_from_tone(tape(sp, 4), SR, 440)
    ok = np.isfinite(r['speed'])
    truth = np.interp(r['t'][ok], np.arange(4 * SR) / SR, sp)
    assert ok.mean() > 0.9 and np.median(np.abs(r['speed'][ok] / truth - 1)) < 0.005
    back = au.speed_from_tone(tape(-1.5), SR, 440)                 # backwards sounds the same
    assert back['median'] == pytest.approx(1.5, rel=0.005)


def test_speed_from_tone_with_the_mix_on_top():
    rng = np.random.default_rng(3)
    x = tape(1.25) + 0.05 * np.sin(2 * np.pi * 97 * np.arange(3 * SR) / SR) + 0.01 * rng.standard_normal(3 * SR)
    assert au.speed_from_tone(x, SR, 440)['median'] == pytest.approx(1.25, rel=0.005)


def test_bad_samples():
    x = sine(secs=1.0)
    x[100], x[200] = np.nan, np.inf
    x[5000:5200] = 0
    x[9000:9100] = 0.25
    x[12000:12010] = 0                                              # too short to count
    b = au.bad_samples(x, SR)
    json.dumps(b)
    assert b['nan'] == 1 and b['inf'] == 1
    assert b['zero_runs'] == [{'at': pytest.approx(5000 / SR), 'len': 200}] and b['zero_samples'] == 200
    assert b['stuck_runs'] == [{'at': pytest.approx(9000 / SR), 'len': 100}]
    assert au.bad_samples(x)['zero_runs'][0]['at'] == 5000
