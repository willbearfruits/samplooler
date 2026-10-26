"""The Python decoders against the firmware's own encoders (rig.c and rig_codec.h, built into the twin)."""
import subprocess
import time

import numpy as np
import pytest

from slrig import port, wire


@pytest.fixture(scope='module')
def twin_bytes():
    exe = port.ensure_twin()
    msgs = wire.go(True, True, True, True) + wire.info() + wire.tune_list() + wire.describe()
    # an injected tilt (nose down 30 degrees, chip axes: x = device y) for 0.3 s, then the scene again
    g = 16384 * np.array([0.0, 0.0, 1.0])
    s = np.sin(np.radians(30)); c = np.cos(np.radians(30))
    raw = np.tile(np.array([16384 * s, 0, 16384 * c, 0, 0, 0]).round().astype(np.int16), (125, 1))   # the board queues 128
    for m in wire.inject(raw):
        msgs += m
    msgs += wire.key_enc(3) + wire.key_switch(True)
    p = subprocess.run([str(exe), '--fast', '--secs', '1.5'], input=msgs, capture_output=True, timeout=30)
    return p.stdout


def _all(data):
    out = {}
    for kind, *rest in wire.Parser().feed(data):
        if kind == 'sysex' and rest[0][:1] == b'\x7d':
            out.setdefault(chr(rest[0][1]), []).append(rest[0][2:])
    return out


def test_every_stream_arrives(twin_bytes):
    m = _all(twin_bytes)
    for k in 'rqoQtIDL':
        assert k in m, f'no {k!r} message'


def test_motion_decodes_and_counts(twin_bytes):
    m = _all(twin_bytes)
    rs = [wire.decode_r(p) for p in m['r']]
    n = sum(len(r['raw']) for r in rs)
    # 1.5 s at 500 Hz is 750; while an injection is live (0.5 s after the last 'J') the sensor's samples are not used,
    # so the 125 injected ones stand in for about 250
    assert 560 <= n <= 760, n
    seqs = [r['seq'] for r in rs]
    assert all((b - a) % 128 == 1 for a, b in zip(seqs, seqs[1:]))
    assert {r['rate_hz'] for r in rs} == {500}
    inj = [r for r in rs if r['flags'] & 1]
    assert inj, 'the injected samples are flagged'
    x = np.concatenate([r['raw'] for r in inj])
    assert abs(np.median(x[:, 0]) - 16384 * 0.5) < 2 and abs(np.median(x[:, 2]) - 16384 * np.cos(np.radians(30))) < 2


def test_state_follows_the_injected_tilt(twin_bytes):
    m = _all(twin_bytes)
    q = wire.decode_Q(m['Q'][0])
    names = [f for f, _ in q['fields']]
    assert names[:3] == ['state', 'mode', 'flags'] and 'speed_cmd' in names
    states = [wire.decode_q(p, q['fields']) for p in m['q']]
    injected = [s for s in states if int(s['flags']) & 4]
    assert injected, 'the state says when motion is injected'
    # ACC (v1) smooths 0.25 a tick: after 0.25 s at 50 Hz (12 ticks) it has come 97 % of the way to 30
    assert 28.0 < injected[-1]['pitch'] < 30.5, injected[-1]['pitch']
    assert injected[-1]['zone'] == 1 and injected[-1]['speed_cmd'] > 0.5     # forward


def test_tune_table_and_settings(twin_bytes):
    m = _all(twin_bytes)
    items = [wire.tune_item(wire.text_kv(p)) for p in m['t'] if b'\nn=' in p]
    names = [i['name'] for i in items]
    assert 'tilt.dead' in names and 'motion.fusion' in names and 'enc.hold' in names
    dead = items[names.index('tilt.dead')]
    assert dead['def'] == 5 and dead['unit'] == '°' and dead['kind'] == 'float'
    fusion = items[names.index('motion.fusion')]
    assert fusion['labels'] == ['ACC', 'COMP', 'MAHONY']
    assert any(b'end=' in p for p in m['t'])
    d = [wire.text_kv(p) for p in m['D']]
    assert any(x.get('name') == 'Tilt' for x in d) and any('end' in x for x in d)


def test_screen_pages(twin_bytes):
    m = _all(twin_bytes)
    pages = {}
    for p in m['o']:
        page, _, data = wire.decode_o(p)
        pages[page] = data
    assert set(pages) == set(range(8))
    assert sum(bin(b).count('1') for d in pages.values() for b in d) > 50     # something is drawn (the frame itself: test_daemon, the PNG)


def test_keys_reach_the_gestures(twin_bytes):
    m = _all(twin_bytes)
    ev = [wire.text_kv(p) for p in m.get('e', [])]
    names = [e['n'] for e in ev]
    assert 'enc.turn' in names and 'enc.down' in names
    assert float(ev[names.index('enc.turn')]['v']) == 3


def test_pack_roundtrips():
    for v in (-32768, -1, 0, 1, 12345, 32767):
        assert wire.get16(wire.put16(v)) == v
    src = bytes(range(256))[:128]
    packed = bytearray()
    for i in range(0, 128, 7):
        grp = src[i:i + 7]
        packed.append(sum(((b >> 7) & 1) << j for j, b in enumerate(grp)))
        packed += bytes(b & 0x7F for b in grp)
    assert wire.unpack7(bytes(packed)) == src
    assert len(packed) == 147
