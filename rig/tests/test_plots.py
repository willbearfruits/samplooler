"""plots.py: every plot writes a real PNG with something drawn on it, and no pyplot window is involved."""
import json
import subprocess
import sys

import numpy as np
import pytest
from matplotlib import image as mpimg

from slrig import audio_analysis as au
from slrig import guided, plots, synth


@pytest.fixture(scope='module')
def twin():
    if not synth.TWIN.exists():
        subprocess.run(['sh', str(synth.RIG / 'twin' / 'build.sh')], capture_output=True)
    if not synth.TWIN.exists():
        pytest.skip('the twin is not built (rig/twin/build.sh)')
    return synth.TWIN


def check_png(path, min_kb=15):
    with open(path, 'rb') as f:
        assert f.read(8) == b'\x89PNG\r\n\x1a\n'
    img = mpimg.imread(path)
    assert img.shape[0] > 300 and img.shape[1] > 800
    assert img[..., :3].std() > 0.02                         # not one flat colour
    import os
    assert os.path.getsize(path) > min_kb * 1024


@pytest.fixture(scope='module')
def steps_session(tmp_path_factory, twin):
    act = guided.act_out('steps')
    ev = [{'t': 4.6, 'name': 'tilt.zone', 'v': 1, 'text': 'fwd'}, {'t': 9.0, 'name': 'rec.down'},
          {'t': 11.0, 'name': 'rec.up'}, {'t': 2.0, 'name': 'enc.tap', 'ms': 120}, {'t': 3.0, 'name': 'odd.thing'}]
    return synth.write_session(tmp_path_factory.mktemp('p') / 'steps', act['raw'], state=synth.twin_states(act['raw']),
                               test=act['test'], events=ev)


def test_plot_session(steps_session, tmp_path):
    out = plots.plot_session(steps_session, out_png=tmp_path / 'a.png', title='Tilt steps')
    assert out == str(tmp_path / 'a.png')
    check_png(out)


def test_plot_session_every_panel_and_a_window(steps_session, tmp_path):
    out = plots.plot_session(steps_session, fields=('acc', 'gyro', 'angles', 'speed', 'fx', 'joy', 'yaw', 'levels'),
                             t0=3, t1=13, out_png=tmp_path / 'b.png')
    check_png(out)


def test_plot_session_default_path_and_empty(steps_session, tmp_path):
    out = plots.plot_session(steps_session, fields=('angles',))
    assert out.endswith('plot-angles.png')
    check_png(out, min_kb=5)
    (tmp_path / 'e').mkdir()
    check_png(plots.plot_session(tmp_path / 'e', out_png=tmp_path / 'e.png'), min_kb=5)


def test_plot_curve(tmp_path, twin):
    c = synth.twin_curve(tune={'tilt.detents': 2})
    out = plots.plot_curve(c, tmp_path / 'c.png', live={'pitch': 22, 'speed': 0.5, 'roll': -25})
    check_png(out)
    (tmp_path / 'c.json').write_text(json.dumps(c))
    check_png(plots.plot_curve(str(tmp_path / 'c.json'), tmp_path / 'c2.png', live=(10, 0.2)))
    check_png(plots.plot_curve(json.dumps(c), tmp_path / 'c3.png'))


def test_plot_replays(tmp_path, twin):
    raw = synth.gesture('free', 10)
    paths = {}
    for label, tune in (('v1', None), ('COMP', {'motion.fusion': 1}), ('MAHONY', {'motion.fusion': 2})):
        st = synth.twin_states(raw, tune=tune)
        p = tmp_path / f'replay-{label}.csv'
        cols = ['t', 'pitch', 'roll', 'speed_cmd']
        np.savetxt(p, np.column_stack([st[c] for c in cols]), delimiter=',', header='t,pitch,roll,speed', comments='')
        paths[label] = p
    out = plots.plot_replays(paths, tmp_path / 'r.png', fields=('pitch', 'roll', 'speed'))
    check_png(out)


def test_plot_levels(tmp_path):
    sr = 32000
    x = 0.4 * np.sin(2 * np.pi * 440 * np.cumsum(np.linspace(0.5, 2, 3 * sr)) / sr)
    x[int(1.5 * sr):] *= -1                                    # a click
    sp = au.speed_from_tone(x, sr, 440)
    out = plots.plot_levels(x, sr, tmp_path / 'l.png', clicks=au.clicks(x, sr), speed=sp)
    check_png(out)
    w = au.write_wav(tmp_path / 's.wav', np.column_stack([x, 0.5 * x]), sr)
    check_png(plots.plot_levels(w, out_png=tmp_path / 'l2.png'))


def test_no_pyplot():
    assert 'matplotlib.pyplot' not in sys.modules or not getattr(plots, 'plt', None)
