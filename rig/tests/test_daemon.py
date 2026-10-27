"""slrigd end to end on the twin: a daemon on a socket of its own, the client, the commands."""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

RIG = Path(__file__).resolve().parent.parent


@pytest.fixture(scope='module')
def daemon(tmp_path_factory):
    run = Path(f"/run/user/{os.getuid()}") if Path(f"/run/user/{os.getuid()}").exists() else Path('/tmp')
    d = run / f'slrig-pytest-{os.getpid()}'
    d.mkdir(exist_ok=True)
    env = dict(os.environ, SLRIG_SOCKET=str(d / 's.sock'), SLRIG_SESSIONS=str(tmp_path_factory.mktemp('sessions')))
    p = subprocess.Popen([sys.executable, '-m', 'slrig.cli', 'serve', '--source', 'twin'], cwd=RIG, env=env,
                         stdout=subprocess.DEVNULL, stderr=open(d / 'log', 'w'))
    for _ in range(80):
        if (d / 's.sock').exists():
            break
        time.sleep(0.1)
    os.environ.update({k: env[k] for k in ('SLRIG_SOCKET', 'SLRIG_SESSIONS')})
    from slrig.client import Client
    c = Client('pytest', autostart=False)
    time.sleep(1.0)
    yield c
    c.close()
    p.terminate()
    p.wait(5)
    import shutil
    shutil.rmtree(d, ignore_errors=True)


def ok(env):
    assert env['ok'], env.get('error')
    return env['result']


def test_status_and_rates(daemon):
    time.sleep(1.2)
    st = ok(daemon.run('status'))
    assert st['source'] == 'twin'
    assert 450 < st['rates']['motion_hz'] < 560
    assert 40 < st['rates']['state_hz'] < 60
    assert not st['drops']


def test_tune_set_by_label_and_back(daemon):
    assert ok(daemon.run('tune.set', {'name': 'motion.fusion', 'value': 'MAHONY'}))['value'] == 2
    assert ok(daemon.run('tune.set', {'name': 'imu.rate', 'value': '1000'}))['value'] == 3
    assert ok(daemon.run('tune.set', {'name': 'tilt.dead', 'value': 99}))['value'] == 30       # clamped
    changed = {i['name'] for i in ok(daemon.run('tune.list', {'changed': True}))}
    assert changed == {'motion.fusion', 'imu.rate', 'tilt.dead'}
    ok(daemon.run('tune.reset'))
    assert ok(daemon.run('tune.list', {'changed': True})) == []
    err = daemon.run('tune.set', {'name': 'tilt.ded', 'value': 3})
    assert not err['ok'] and err['error']['code'] == 'tune.unknown'


def test_presets_and_ab(daemon):
    got = ok(daemon.run('tune.preset', {'name': 'gyro'}))['changed']
    assert got.get('motion.fusion') == 1 and got.get('motion.rate') == 1
    ok(daemon.run('tune.ab', {'slot': 'A', 'store': True}))
    ok(daemon.run('tune.reset'))
    ok(daemon.run('tune.ab', {'slot': 'A'}))
    assert ok(daemon.run('tune.get', {'name': 'motion.fusion'}))['value'] == 1
    ok(daemon.run('tune.reset'))


def test_curve_is_the_firmware_curve(daemon):
    c = ok(daemon.run('curve', {'n': 181}))
    a, s = c['tilt']['angle'], c['tilt']['speed']
    assert s[a.index(0.0)] == pytest.approx(c['spd_min'])                 # flat: Speed min
    assert s[a.index(45.0)] == pytest.approx(c['spd_max'], rel=1e-3)      # 45 degrees nose down: Speed max
    assert s[a.index(-45.0)] == pytest.approx(-c['spd_max'], rel=1e-3)
    d, st = c['roll']['delay'], c['roll']['stutter']
    assert d[c['roll']['angle'].index(-30.0)] == pytest.approx(0.5, abs=0.01) and st[c['roll']['angle'].index(30.0)] == pytest.approx(0.5, abs=0.01)


def test_keys_fire_gestures(daemon):
    ok(daemon.run('key', {'tap': True}))
    time.sleep(0.6)
    ok(daemon.run('key', {'hold_ms': 650}))
    time.sleep(0.3)
    names = [e['name'] for e in ok(daemon.run('events.tail', {'n': 20}))]
    assert 'enc.tap' in names and 'menu.open' in names and 'enc.hold' in names and 'menu.close' in names
    g = {x['name']: x for x in ok(daemon.run('gestures'))}
    assert g['enc.tap']['count'] >= 1 and '40..500 ms' in g['enc.tap']['rule']


def test_record_and_replay(daemon):
    ok(daemon.run('record.start', {'name': 'pytest'}))
    time.sleep(2.0)
    s = ok(daemon.run('record.stop'))
    assert s['motion'] > 800 and s['state'] > 80 and s['screens'] > 3
    p = Path(s['path'])
    meta = json.loads((p / 'meta.json').read_text())
    assert meta['imu']['rate_hz'] == 500 and meta['tune']['tilt.dead'] == 5
    r = ok(daemon.run('session.replay', {'name': s['name'], 'tune': 'v1'}))
    assert r['ticks'] > 80
    lines = Path(r['csv']).read_text().splitlines()
    assert lines[0].startswith('t,pitch,roll')


def test_screen_png(daemon):
    r = ok(daemon.run('screen.png', {'scale': 2}))
    b = Path(r['png']).read_bytes()
    assert b[:8] == b'\x89PNG\r\n\x1a\n' and len(b) > 300
