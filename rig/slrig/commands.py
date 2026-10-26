"""The rig's commands (registry.py). Each is fn(rig, **typed args) and returns plain JSON. `slrig agents-md` lists them."""
from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import threading
import time
import zlib
import struct
from pathlib import Path
from typing import Literal

import numpy as np

from . import port as portmod
from . import sessions as S
from . import wire
from .registry import REGISTRY, RigError, command, get
from .rig import Rig

RIG_DIR = Path(__file__).resolve().parent.parent
PRESETS = {'v1': 0, 'gyro': 1, 'gyro detents': 2, 'mahony': 3}


def _need_port(rig: Rig) -> None:
    if not rig.port:
        raise RigError('board.none', 'nothing connected', 'run connect (source=auto finds a board, else starts the twin)')


def _item(rig: Rig, name: str) -> dict:
    i = rig.tune_index(name)
    if i is None:
        close = [it['name'] for it in rig.tune.values() if name.split('.')[0] in it['name']][:8]
        raise RigError('tune.unknown', f"no tuning value '{name}'", 'tune.list shows them' + (': ' + ', '.join(close) if close else ''))
    return rig.tune[i]


# ---------------------------------------------------------------- the connection
@command('status', group='board', examples=[{}])
def status(rig: Rig) -> dict:
    """Where the rig stands: the source (board, twin or none), the board, stream rates and drops, recording, OSC, and
    the latest state in short."""
    st = rig.status()
    s = rig.state
    st['now'] = {k: s.get(k) for k in ('state', 'mode', 'pitch', 'roll', 'yaw', 'speed_cmd', 'speed_now', 'zone', 'delay',
                                       'stutter', 'frozen', 'menu', 'rest', 'cpu', 'heap_kb', 'i2c_fail') if k in s}
    return st


@command('boards', group='board')
def boards(rig: Rig) -> list:
    """The Samplooler boards on USB (a TWOFOLD is never listed)."""
    return portmod.boards()


@command('connect', group='board', examples=[{'source': 'auto'}, {'source': 'twin', 'scene': 'demo'}])
def connect(rig: Rig, source: Literal['auto', 'board', 'twin'] = 'auto', serial: str = '',
            scene: Literal['still', 'demo'] = 'still') -> dict:
    """Open the Samplooler on USB (found by its MIDI port's name, never a TWOFOLD), or start the twin when there is
    none (or when asked).

    Args:
        source: auto: a board if one is plugged in, else the twin; board: a board or an error; twin: the twin.
        serial: part of the board's USB serial when more than one is plugged in.
        scene: the twin's motion when nothing is injected: still (on a table) or demo (a 32 s performance).
    """
    return rig.connect(source, serial, scene)


@command('disconnect', group='board')
def disconnect(rig: Rig) -> dict:
    """Let go of the board (its streams stop) or stop the twin."""
    rig.disconnect()
    rig.push_status()
    return {'source': 'none'}


@command('stream', group='board')
def stream(rig: Rig, motion: bool | None = None, state: bool | None = None, screen: bool | None = None,
           events: bool | None = None) -> dict:
    """Which of the board's streams run (all four by default). Off saves USB bandwidth.

    Args:
        motion: the raw IMU samples ('r').
        state: what the controls decided, 50 or 100 times a second ('q').
        screen: the OLED's frames ('o').
        events: the gestures as they fire ('e').
    """
    for k, v in (('motion', motion), ('state', state), ('screen', screen), ('events', events)):
        if v is not None:
            rig.want[k] = bool(v)
    if rig.port:
        rig._send(wire.go(**rig.want))
    return dict(rig.want)


# ---------------------------------------------------------------- the tuning table
@command('tune.list', group='tune')
def tune_list(rig: Rig, group: str = '', changed: bool = False) -> list:
    """The tuning table as the board has it: name, kind, range, step, default, value, unit, help.

    Args:
        group: only this group (imu, motion, tilt, roll, joy, enc, rec, mic, usb, rig).
        changed: only the values that differ from the defaults (v1).
    """
    items = [rig.tune[i] for i in sorted(rig.tune)]
    if group:
        items = [it for it in items if it['group'] == group]
    if changed:
        items = [it for it in items if it['kind'] != 'action' and it['value'] != it['def']]
    return items


@command('tune.get', group='tune')
def tune_get(rig: Rig, name: str) -> dict:
    """One tuning value.

    Args:
        name: e.g. tilt.dead, motion.fusion, enc.hold.
    """
    return _item(rig, name)


@command('tune.set', group='tune', examples=[{'name': 'tilt.dead', 'value': 6.5}, {'name': 'motion.fusion', 'value': 'COMP'}])
def tune_set(rig: Rig, name: str, value: str) -> dict:
    """Change a tuning value on the board, at once (not saved until tune.save). Enums take their label or index;
    an action runs with any value.

    Args:
        name: the value's name (tune.list).
        value: a number, or an enum's label (e.g. COMP, "8 g", 500).
    """
    _need_port(rig)
    it = _item(rig, name)
    v: float
    if it['kind'] in ('enum', 'bool') and it['labels']:
        labels = [l.lower().replace(' ', '') for l in it['labels']]
        sv = str(value).strip().lower().replace(' ', '')
        if sv in labels:                               # a label wins: COMP, 8g, 500, +1
            v = it['min'] + labels.index(sv)
        else:
            try:
                v = float(value)                       # else the number is the index
            except ValueError:
                raise RigError('tune.value', f"{name} is one of {', '.join(it['labels'])}", '')
    else:
        try:
            v = float(value)
        except ValueError:
            raise RigError('tune.value', f'{name} takes a number', f"{it['min']}..{it['max']} {it['unit']}")
    before = it['value']
    rig._send(wire.tune_set(it['i'], v))
    if it['kind'] == 'action':
        return {'name': name, 'ran': True}
    target = min(max(v, it['min']), it['max'])
    ok = rig._wait(lambda: rig.tune[it['i']]['value'] != before or abs(rig.tune[it['i']]['value'] - target) < 1e-6, 1.0)
    out = dict(rig.tune[it['i']])
    if not ok:
        out['note'] = 'the board did not confirm within 1 s'
    return out


@command('tune.save', group='tune')
def tune_save(rig: Rig) -> dict:
    """Keep the board's current tuning in its flash (NVS "tune"), for the next power-on."""
    _need_port(rig)
    rig._send(wire.tune_save())
    return {'asked': True, 'changed': [it['name'] for it in tune_list(rig, changed=True)]}


@command('tune.reset', group='tune')
def tune_reset(rig: Rig) -> dict:
    """All tuning values back to the defaults: the firmware as it was before the table (v1). Not saved."""
    _need_port(rig)
    rig.tune_listing = None
    rig._send(wire.tune_defaults())
    rig._wait(lambda: rig.tune_listing is not None, 3.0)
    return {'changed': len(tune_list(rig, changed=True))}


@command('tune.preset', group='tune', examples=[{'name': 'gyro'}])
def tune_preset(rig: Rig, name: str) -> dict:
    """Load a preset onto the board: v1 (today's feel), gyro (COMP fusion at 100 Hz), gyro detents, mahony.

    Args:
        name: v1, gyro, gyro detents or mahony (or its number).
    """
    _need_port(rig)
    p = PRESETS.get(name.lower(), None)
    if p is None:
        try:
            p = int(name)
        except ValueError:
            raise RigError('tune.preset', f'no preset {name!r}', 'v1, gyro, gyro detents, mahony')
    rig.tune_listing = None
    rig._send(wire.tune_preset(p))
    rig._wait(lambda: rig.tune_listing is not None, 3.0)
    return {'preset': name, 'changed': {it['name']: it['value'] for it in tune_list(rig, changed=True)}}


@command('tune.ab', group='tune')
def tune_ab(rig: Rig, slot: Literal['A', 'B'], store: bool = False) -> dict:
    """Two whole tuning sets to flip between while listening: store the board's current set in A or B, or recall one.

    Args:
        slot: A or B.
        store: true keeps the current set there; false (the default) loads it onto the board.
    """
    _need_port(rig)
    if store:
        rig.ab[slot] = {it['name']: it['value'] for it in rig.tune.values() if it['kind'] != 'action'}
        return {'stored': slot, 'changed': {k: v for k, v in rig.ab[slot].items() if v != rig.tune[rig.tune_index(k)]['def']}}
    want = rig.ab.get(slot)
    if not want:
        raise RigError('tune.ab_empty', f'nothing stored in {slot}', f'tune.ab slot={slot} store=true first')
    moved = []
    for n, v in want.items():
        i = rig.tune_index(n)
        if i is not None and rig.tune[i]['value'] != v:
            rig._send(wire.tune_set(i, v))
            moved.append(n)
    rig._wait(lambda: all(rig.tune[rig.tune_index(n)]['value'] == want[n] for n in moved), 2.0)
    return {'recalled': slot, 'moved': moved}


@command('tune.export', group='tune')
def tune_export(rig: Rig, path: str = '') -> dict:
    """Write the current tuning as JSON and as the C lines that would make it the firmware's defaults (for a review
    before anyone edits tune.h). Nothing on the board changes.

    Args:
        path: where to write the JSON (default: the sessions folder, tune-<time>.json).
    """
    items = tune_list(rig, changed=True)
    p = Path(path).expanduser() if path else S.sessions_dir() / time.strftime('tune-%Y%m%d-%H%M%S.json')
    p.write_text(json.dumps({it['name']: it['value'] for it in items}, indent=1))
    c = '\n'.join(f"// {it['name']}: {it['def']:g} -> {it['value']:g} {it['unit']}" for it in items)
    p.with_suffix('.c.txt').write_text(c + '\n')
    return {'json': str(p), 'c': str(p.with_suffix('.c.txt')), 'changed': {it['name']: it['value'] for it in items}}


# ---------------------------------------------------------------- the board's own settings
@command('settings.list', group='settings')
def settings_list(rig: Rig) -> list:
    """The board's settings (the menu's), as text: Mode, Tilt, Speed max, ... They are stepped, not set."""
    return [rig.settings[i] for i in sorted(rig.settings)]


@command('settings.step', group='settings', examples=[{'index': 5, 'delta': 1}])
def settings_step(rig: Rig, index: int, delta: int = 1) -> dict:
    """Step one of the board's settings as the encoder would (an action, such as Save, runs with delta 1).

    Args:
        index: the setting's i (settings.list).
        delta: +1 or -1.
    """
    _need_port(rig)
    if index not in rig.settings:
        raise RigError('settings.unknown', f'no setting {index}', 'settings.list')
    before = rig.settings[index]['text']
    rig._send(wire.step(index, 1 if delta > 0 else -1))
    rig._wait(lambda: rig.settings[index]['text'] != before, 1.0)
    return rig.settings[index]


def _setting_value(rig: Rig, name: str, default: float) -> float:
    for s in rig.settings.values():
        if s['name'] == name:
            try:
                return float(''.join(c for c in s['text'] if c in '0123456789.-') or default)
            except ValueError:
                return default
    return default


# ---------------------------------------------------------------- hands from the PC
@command('key', group='hands', examples=[{'enc': 3}, {'tap': True}, {'hold_ms': 800}, {'rec': True}])
def key(rig: Rig, enc: int = 0, enc_down: bool | None = None, rec: bool | None = None, tap: bool = False,
        hold_ms: int = 0, press_ms: int = 0) -> dict:
    """Turn and press the encoder, or press REC, from the PC. The board's gesture code sees them as if from the pins.

    Args:
        enc: detents to turn (negative = left).
        enc_down: press (true) or let go (false) of the encoder.
        rec: press (true) or let go (false) of REC.
        tap: a tap of the encoder (down 120 ms, up).
        hold_ms: hold the encoder this long, then let go.
        press_ms: with rec=true: let go of REC after this long (a take of that length).
    """
    _need_port(rig)
    if enc:
        rig._send(wire.key_enc(enc))
    if enc_down is not None:
        rig._send(wire.key_switch(enc_down))
    if tap:
        rig._send(wire.key_switch(True)); time.sleep(0.12); rig._send(wire.key_switch(False))
    if hold_ms:
        rig._send(wire.key_switch(True)); time.sleep(hold_ms / 1000); rig._send(wire.key_switch(False))
    if rec is not None:
        rig._send(wire.key_rec(rec))
        if rec and press_ms:
            time.sleep(press_ms / 1000)
            rig._send(wire.key_rec(False))
    return {'sent': True}


@command('joy.override', group='hands')
def joy_override(rig: Rig, x: int | None = None, y: int | None = None) -> dict:
    """Stand in for the joystick's ADC (0..4095 each, 2048 is the centre); no arguments ends it. joy.on must be set.

    Args:
        x: raw X.
        y: raw Y.
    """
    _need_port(rig)
    rig._send(wire.key_joy(x, y))
    return {'x': x, 'y': y}


class _Injector:
    """Plays raw chip samples into the board at their own rate: 'J' messages every 10 ms."""

    def __init__(self, rig: Rig, raw: np.ndarray, rate: int, label: str):
        self.rig, self.raw, self.rate, self.label = rig, np.asarray(raw, dtype=np.int16), rate, label
        self.stop = threading.Event()
        self.done = threading.Event()
        self.thread = threading.Thread(target=self._run, name='inject', daemon=True)

    def _run(self):
        rig = self.rig
        rig.busy['inject'] = {'what': self.label, 'secs': round(len(self.raw) / self.rate, 2)}
        rig.push_status()
        t0, i, n = time.monotonic(), 0, len(self.raw)
        try:
            while i < n and not self.stop.is_set():
                due = int((time.monotonic() - t0) * self.rate) + 1
                j = min(n, max(due, i))
                if j > i:
                    for msg in wire.inject(self.raw[i:j]):
                        rig._send(msg)
                    i = j
                time.sleep(0.008)
        except (RigError, OSError, TimeoutError):
            pass
        finally:
            rig.busy.pop('inject', None)
            rig.push_status()
            self.done.set()


def _inject(rig: Rig, raw: np.ndarray, rate: int, label: str, wait: bool) -> dict:
    _need_port(rig)
    old = rig.busy.get('_injector')
    if old:
        old.stop.set(); old.done.wait(1.0)
    inj = _Injector(rig, raw, rate, label)
    rig.busy['_injector'] = inj
    inj.thread.start()
    if wait:
        inj.done.wait()
    rig.busy.pop('_injector', None) if wait else None
    return {'injecting': label, 'samples': len(raw), 'secs': round(len(raw) / rate, 2), 'rate_hz': rate}


def _imu_now(rig: Rig) -> tuple[int, int, int]:
    def v(n, d):
        i = rig.tune_index(n)
        return int(rig.tune[i]['value']) if i is not None else d
    return wire.RATES[v('imu.rate', 2)], v('imu.acc_range', 0), v('imu.gyro_range', 2)


@command('inject.gesture', group='inject', slow=True,
         examples=[{'kind': 'tilt', 'pitch': 30, 'secs': 3}, {'kind': 'flicks', 'amp': 20, 'secs': 4}, {'kind': 'shake', 'amp': 0.6}])
def inject_gesture(rig: Rig, kind: str = 'still', secs: float = 3.0, pitch: float = 0.0, roll: float = 0.0,
                   amp: float = 0.0, seed: int = 0, wait: bool = False) -> dict:
    """Act out a gesture in the board's sensor stream (a synthetic hand: the twin's model, with noise and bias). The
    board's own motion and gesture code takes it as real. Kinds: still, tilt, steps, sweep, flicks, shake, roll, axes,
    free.

    Args:
        kind: the gesture.
        secs: how long.
        pitch: degrees, nose down + (tilt, sweep end).
        roll: degrees, right edge down + (tilt, roll).
        amp: the size of flicks (deg) or a shake (g).
        seed: the noise's seed.
        wait: answer when it is over.
    """
    from . import synth
    rate, ar, gr = _imu_now(rig)
    kw = {}
    if pitch:
        kw['pitch'] = pitch
    if roll:
        kw['roll'] = roll
    if amp:
        kw['amp'] = amp
    raw = synth.gesture(kind, secs, rate_hz=rate, acc_range=ar, gyro_range=gr, seed=seed, **kw)
    return _inject(rig, raw, rate, f'{kind} {secs:g} s', wait)


@command('inject.take', group='inject', slow=True)
def inject_take(rig: Rig, session: str = 'last', t0: float = 0.0, t1: float = 0.0, wait: bool = False) -> dict:
    """Play a recorded hand (a session's raw.txt) back into the board: the same gesture through other tuning, on the
    real firmware.

    Args:
        session: the session's name (or a unique part of it, or last).
        t0: from this second.
        t1: to this second (0: the end).
        wait: answer when it is over.
    """
    p = S.resolve(session)
    meta = json.loads((p / 'meta.json').read_text())
    rate = int(meta.get('imu', {}).get('rate_hz', 500))
    raw = np.loadtxt(p / 'raw.txt', dtype=np.int64, ndmin=2)
    a, b = int(t0 * rate), int(t1 * rate) if t1 else len(raw)
    return _inject(rig, raw[a:b], rate, f'take {p.name}', wait)


@command('inject.stop', group='inject')
def inject_stop(rig: Rig) -> dict:
    """Stop an injection: the sensor is back within half a second."""
    inj = rig.busy.pop('_injector', None)
    if inj:
        inj.stop.set()
    return {'stopped': bool(inj)}


# ---------------------------------------------------------------- recordings
def _meta(rig: Rig, test: dict | None = None, notes: str = '') -> dict:
    rate, ar, gr = _imu_now(rig)
    return {'source': rig.source, 'board': dict(rig.board), 'tune': {it['name']: it['value'] for it in rig.tune.values()},
            'settings': {s['name']: s['text'] for s in rig.settings.values()},
            'imu': {'rate_hz': rate, 'acc_lsb': wire.ACC_LSB[ar], 'gyro_lsb': wire.GYRO_LSB[gr], 'axis_map': list(rig.axis_map())},
            'test': test, 'injected': False, 'notes': notes}


@command('record.start', group='record', examples=[{'name': 'slow tilts', 'secs': 20}])
def record_start(rig: Rig, name: str = 'take', secs: float = 0.0, notes: str = '') -> dict:
    """Start recording everything the board says (motion, state, events, screen) into a session folder.

    Args:
        name: a few words for the folder's name.
        secs: stop by itself after this long (0: until record.stop).
        notes: what you are doing, for later.
    """
    _need_port(rig)
    with rig.lock:
        if rig.recorder:
            raise RigError('record.busy', f'already recording {rig.recorder.name}', 'record.stop first')
        rig.recorder = S.SessionWriter(name, _meta(rig, notes=notes), [f for f, _ in rig.fields])
    rig.push('session', {'state': 'recording', 'name': rig.recorder.name, 'secs': 0})
    if secs > 0:
        rec = rig.recorder

        def stop_later():
            end = time.monotonic() + secs
            while time.monotonic() < end and rig.recorder is rec:
                time.sleep(0.25)
                rig.push('session', {'state': 'recording', 'name': rec.name, 'secs': round(rec.t_last, 1)})
            if rig.recorder is rec:
                rig.run('record.stop', {}, 'rig')
        threading.Thread(target=stop_later, daemon=True).start()
    return {'recording': rig.recorder.name, 'path': str(rig.recorder.path)}


@command('record.stop', group='record')
def record_stop(rig: Rig) -> dict:
    """Stop recording; the session's summary."""
    with rig.lock:
        rec, rig.recorder = rig.recorder, None
    if not rec:
        raise RigError('record.idle', 'not recording', 'record.start')
    out = rec.close(injected=bool(rig.busy.get('inject')))
    rig.push('session', {'state': 'idle', 'name': rec.name, 'secs': out['secs']})
    return out


@command('session.list', group='session')
def session_list(rig: Rig, limit: int = 30) -> list:
    """The recordings, newest first.

    Args:
        limit: how many.
    """
    return S.list_sessions(limit)


@command('session.info', group='session')
def session_info(rig: Rig, name: str = 'last') -> dict:
    """A session's meta (tuning and settings at the time, the test and its steps) and, if analysed, its analysis.

    Args:
        name: the session (a unique part of its name, or last).
    """
    p = S.resolve(name)
    out = json.loads((p / 'meta.json').read_text())
    out['path'] = str(p)
    if (p / 'analysis.json').exists():
        out['analysis'] = json.loads((p / 'analysis.json').read_text())
    return out


@command('session.notes', group='session')
def session_notes(rig: Rig, name: str = 'last', text: str = '') -> dict:
    """Add a line to a session's notes (what it felt like, what changed).

    Args:
        name: the session.
        text: the note.
    """
    p = S.resolve(name)
    m = json.loads((p / 'meta.json').read_text())
    m['notes'] = (m.get('notes', '') + '\n' + text).strip()
    (p / 'meta.json').write_text(json.dumps(m, indent=1))
    return {'name': p.name, 'notes': m['notes']}


@command('session.analyze', group='session', slow=True)
def session_analyze(rig: Rig, name: str = 'last') -> dict:
    """Analyse a session: its guided test's own analysis if it was one, else the general look (rest noise and bias,
    drift, the gestures that fired). Written to analysis.json.

    Args:
        name: the session.
    """
    from . import analysis, guided
    p = S.resolve(name)
    sess = analysis.load_session(p)
    meta = json.loads((p / 'meta.json').read_text())
    test = (meta.get('test') or {}).get('name')
    if test and test in guided.TESTS:
        out = guided.TESTS[test].analyze(sess)
    else:
        out = {'test': None, 'rest': analysis.rest_stats(sess), 'drift': analysis.drift(sess),
               'encoder': analysis.encoder_stats(sess.events), 'rec': analysis.rec_stats(sess.events)}
    (p / 'analysis.json').write_text(json.dumps(out, indent=1, default=float))
    return out


def _calc(rig: Rig) -> portmod.Calc:
    if rig.calc is None or rig.calc.proc.poll() is not None:
        rig.calc = portmod.Calc()
    return rig.calc


def _tune_set_for(rig: Rig, which: str, meta: dict) -> dict[str, float]:
    """A named tuning set: current (the board now), recorded (the session's), v1, a preset, A or B."""
    if which == 'current':
        return {it['name']: it['value'] for it in rig.tune.values()}
    if which == 'recorded':
        return dict(meta.get('tune', {}))
    if which in ('A', 'B'):
        if not rig.ab.get(which):
            raise RigError('tune.ab_empty', f'nothing stored in {which}', f'tune.ab slot={which} store=true')
        return dict(rig.ab[which])
    if which == 'v1':
        return {}
    pre = {'gyro': {'motion.fusion': 1, 'motion.rate': 1, 'motion.comp_tau': 0.6, 'motion.acc_gate': 0.2},
           'mahony': {'motion.fusion': 2, 'motion.rate': 1, 'motion.kp': 2, 'motion.ki': 0.05, 'motion.acc_gate': 0.2}}
    pre['gyro detents'] = dict(pre['gyro'], **{'tilt.detents': 2, 'tilt.detent_w': 0.12})
    if which in pre:
        return pre[which]
    try:
        return json.loads(which)
    except ValueError:
        raise RigError('tune.set_name', f'no tuning set {which!r}', 'current, recorded, v1, gyro, gyro detents, mahony, A, B, or a JSON object')


@command('session.replay', group='session', slow=True, examples=[{'name': 'last', 'tune': 'gyro'}])
def session_replay(rig: Rig, name: str = 'last', tune: str = 'current', label: str = '') -> dict:
    """Replay a session's raw motion through the firmware's own motion.c and gesture.c (the twin's calculator) with a
    tuning set, and write replay-<label>.csv: what the board would have done with that tuning.

    Args:
        name: the session.
        tune: current, recorded, v1, gyro, gyro detents, mahony, A, B, or a JSON object of name: value.
        label: the file's label (default: the set's name).
    """
    p = S.resolve(name)
    meta = json.loads((p / 'meta.json').read_text())
    values = _tune_set_for(rig, tune, meta)
    calc = _calc(rig)
    items = rig.tune_listing or []
    calc.ask('defaults')
    names = {it['name']: it['i'] for it in items} if items else {}
    if not names:                                   # no board: the table's order from the twin's list
        names = {n: i for i, n in enumerate(_tune_names())}
    for n, v in values.items():
        if n in names:
            calc.ask(f'set {names[n]} {v}')
    imu = meta.get('imu', {})
    smin, smax = _speeds(meta)
    label = label or tune.replace(' ', '-')
    out = p / f'replay-{label}.csv'
    r = calc.ask(f"replay {p / 'raw.txt'} {out} {int(imu.get('rate_hz', 500))} {imu.get('acc_lsb', 16384)} "
                 f"{imu.get('gyro_lsb', 32.8)} {smin} {smax}")
    if not r.startswith('ok'):
        raise RigError('replay.failed', r, '')
    return {'csv': str(out), 'ticks': int(r.split()[1]), 'tune': tune, 'changed': values if tune != 'current' else
            {k: v for k, v in values.items() if names and k in names and rig.tune and v != rig.tune[names[k]]['def']}}


def _speeds(meta: dict) -> tuple[float, float]:
    st = meta.get('settings', {})

    def num(s, d):
        try:
            return float(''.join(c for c in str(s) if c in '0123456789.-'))
        except ValueError:
            return d
    return num(st.get('Speed min', '0.05x'), 0.05), num(st.get('Speed max', '2.00x'), 2.0)


_TUNE_NAMES: list[str] = []


def _tune_names() -> list[str]:
    if not _TUNE_NAMES:
        import re
        text = (RIG_DIR.parent / 'firmware' / 'main' / 'tune.h').read_text()
        _TUNE_NAMES.extend(re.findall(r'^X\(\w+,\s*"([^"]+)"', text, re.M))
    return _TUNE_NAMES


@command('session.compare', group='session', slow=True, examples=[{'name': 'last', 'a': 'v1', 'b': 'gyro'}])
def session_compare(rig: Rig, name: str = 'last', a: str = 'v1', b: str = 'gyro', plot: bool = True) -> dict:
    """The same recorded hand through two tuning sets: the board's angles and speed under each, the numbers side by
    side (jitter at rest, lag, how far a shake moves the speed), and a plot.

    Args:
        name: the session.
        a: the first tuning set (as session.replay's tune).
        b: the second.
        plot: also draw both (a PNG beside the session).
    """
    from . import analysis
    ra = session_replay(rig, name, a, 'A-' + a.replace(' ', '-'))
    rb = session_replay(rig, name, b, 'B-' + b.replace(' ', '-'))
    p = S.resolve(name)
    out = {'session': p.name, 'a': a, 'b': b, 'csv': [ra['csv'], rb['csv']]}
    try:
        out['table'] = analysis.compare_replays(p, ra['csv'], rb['csv'])
    except AttributeError:
        out['table'] = _compare_basic(ra['csv'], rb['csv'])
    if plot:
        from . import plots
        out['png'] = plots.plot_replays({a: ra['csv'], b: rb['csv']}, str(p / f'compare-{a}-{b}.png'.replace(' ', '-')))
    return out


def _compare_basic(csv_a: str, csv_b: str) -> list:
    rows = []
    A = np.genfromtxt(csv_a, delimiter=',', names=True)
    B = np.genfromtxt(csv_b, delimiter=',', names=True)
    for f in ('pitch', 'roll', 'speed'):
        da, db = np.std(np.diff(A[f])), np.std(np.diff(B[f]))
        rows.append({'metric': f'{f}: tick-to-tick wobble (std of the difference)', 'A': round(float(da), 4),
                     'B': round(float(db), 4), 'better': 'A' if da < db else 'B'})
    return rows


@command('session.plot', group='session', slow=True)
def session_plot(rig: Rig, name: str = 'last', fields: str = 'acc,gyro,angles,speed', t0: float = 0.0, t1: float = 0.0) -> dict:
    """Draw a session: stacked panels of the chosen fields, gestures along the bottom, test steps shaded. A PNG.

    Args:
        name: the session.
        fields: any of acc, gyro, angles, speed, fx, joy, levels (comma separated).
        t0: from this second.
        t1: to this second (0: the end).
    """
    from . import analysis, plots
    p = S.resolve(name)
    sess = analysis.load_session(p)
    out = p / f"plot-{fields.replace(',', '-')}.png"
    return {'png': plots.plot_session(sess, [f.strip() for f in fields.split(',') if f.strip()], t0 or None, t1 or None,
                                      str(out), p.name)}


# ---------------------------------------------------------------- guided tests
@command('test.list', group='test')
def test_list(rig: Rig) -> list:
    """The guided tests: what each finds out, and its steps."""
    from . import guided
    return [{'name': n, 'title': t.title, 'why': getattr(t, 'why', ''),
             'steps': [{'label': s.label, 'prompt': s.prompt, 'secs': s.secs} for s in t.steps],
             'secs': round(sum(s.secs for s in t.steps), 1)} for n, t in guided.TESTS.items()]


@command('test.run', group='test', slow=True, examples=[{'name': 'still'}, {'name': 'steps', 'inject': True}])
def test_run(rig: Rig, name: str, inject: bool | None = None, countdown: float = 3.0, apply: bool = False) -> dict:
    """Run a guided test: the viewer shows each step's prompt with a countdown, the rig records the whole test, then
    analyses it. On the twin (or with inject=true) the rig acts the steps out itself, so the analysis can be checked
    without hands.

    Args:
        name: the test (test.list).
        inject: act the steps out (default: on the twin yes, on a board no).
        countdown: seconds of "get ready" before each step.
        apply: set the tuning values the analysis suggests (otherwise they are only reported).
    """
    from . import guided
    _need_port(rig)
    if name not in guided.TESTS:
        raise RigError('test.unknown', f'no test {name!r}', ', '.join(guided.TESTS))
    if rig.busy.get('test'):
        raise RigError('test.busy', f"test {rig.busy['test']} is running", 'test.abort')
    test = guided.TESTS[name]
    inject = rig.source == 'twin' if inject is None else inject
    abort = threading.Event()
    rig.busy['test'] = name
    rig.busy['_abort'] = abort
    rig.push_status()
    steps_meta = []
    rec = None
    try:
        if rig.recorder:
            record_stop(rig)
        record_start(rig, f'test-{name}')
        rec = rig.recorder
        rate, ar, gr = _imu_now(rig)
        n = len(test.steps)
        for k, step in enumerate(test.steps, 1):
            for left in range(int(round(countdown)), 0, -1):
                rig.push('prompt', {'test': name, 'step': k, 'of': n, 'text': step.prompt, 'secs': left, 'phase': 'ready'})
                if abort.wait(1.0):
                    raise RigError('test.aborted', 'aborted', '')
            t_start = max(0.0, rig.state_t - (rec.t0 if rec.t0 is not None else rig.state_t))
            synth_kw = getattr(step, 'synth', None)
            if inject and synth_kw:
                from . import synth
                kw = dict(synth_kw)
                kind = kw.pop('kind', 'still')
                kw.pop('secs', None)
                raw = synth.gesture(kind, step.secs, rate_hz=rate, acc_range=ar, gyro_range=gr, seed=k, **kw)
                _inject(rig, raw, rate, f'{name}: {step.label}', False)
            keys = getattr(step, 'keys', None) or []
            if keys:
                threading.Thread(target=_play_keys, args=(rig, keys), daemon=True).start()
            end = time.monotonic() + step.secs
            while (left := end - time.monotonic()) > 0:
                rig.push('prompt', {'test': name, 'step': k, 'of': n, 'text': step.prompt, 'secs': round(left, 1), 'phase': 'go'})
                if abort.wait(min(0.5, left)):
                    raise RigError('test.aborted', 'aborted', '')
            t_end = max(t_start, rig.state_t - (rec.t0 if rec.t0 is not None else rig.state_t))
            steps_meta.append({'t0': round(t_start, 3), 't1': round(t_end, 3), 'label': step.label, 'prompt': step.prompt})
        time.sleep(0.3)
    except RigError:
        rig.push('prompt', {'test': name, 'text': '', 'phase': 'done', 'secs': 0})
        if rig.recorder is rec and rec:
            rig.recorder = None
            rec.close(test={'name': name, 'steps': steps_meta, 'aborted': True})
        raise
    finally:
        rig.busy.pop('test', None); rig.busy.pop('_abort', None)
        inj = rig.busy.pop('_injector', None)
        if inj:
            inj.stop.set()
        rig.push_status()
    with rig.lock:
        if rig.recorder is rec:
            rig.recorder = None
    summary = rec.close(test={'name': name, 'steps': steps_meta}, injected=bool(inject))
    rig.push('session', {'state': 'idle', 'name': rec.name, 'secs': summary['secs']})
    result = session_analyze(rig, rec.name)
    rig.push('prompt', {'test': name, 'text': result.get('summary', 'done'), 'phase': 'done', 'secs': 0})
    out = {'session': rec.name, 'steps': steps_meta, 'analysis': result}
    if apply and result.get('suggest'):
        out['applied'] = {}
        for k2, v in result['suggest'].items():
            out['applied'][k2] = tune_set(rig, k2, str(v)).get('value')
    return out


def _play_keys(rig: Rig, keys: list) -> None:
    t0 = time.monotonic()
    for t, what, v in sorted(keys, key=lambda x: x[0]):
        time.sleep(max(0.0, t - (time.monotonic() - t0)))
        try:
            if what == 'enc':
                rig._send(wire.key_enc(int(v)))
            elif what == 'enc_down':
                rig._send(wire.key_switch(bool(v)))
            elif what == 'rec':
                rig._send(wire.key_rec(bool(v)))
            elif what == 'joy':
                rig._send(wire.key_joy(*(v if v else (None, None))))
        except (RigError, OSError):
            return


@command('test.abort', group='test')
def test_abort(rig: Rig) -> dict:
    """Stop the guided test that is running (its recording is kept, marked aborted)."""
    ev = rig.busy.get('_abort')
    if ev:
        ev.set()
    return {'aborted': bool(ev)}


# ---------------------------------------------------------------- what the board shows, and the curve
def png_bytes(rgb: np.ndarray) -> bytes:
    h, w, _ = rgb.shape
    raw = b''.join(b'\0' + rgb[y].astype(np.uint8).tobytes() for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')


def screen_rgb(fb: bytes, scale: int = 4) -> np.ndarray:
    pages = np.frombuffer(bytes(fb), dtype=np.uint8).reshape(8, 128)
    bits = np.unpackbits(pages[:, None, :], axis=1, bitorder='little').reshape(64, 128).astype(bool)
    img = np.zeros((64, 128, 3), dtype=np.uint8); img[:] = (12, 14, 18)
    on_top, on_body = (255, 214, 64), (120, 210, 255)        # the two-colour panel: a yellow band, then blue
    img[:16][bits[:16]] = on_top
    img[16:][bits[16:]] = on_body
    return img.repeat(scale, 0).repeat(scale, 1)


@command('screen.png', group='view')
def screen_png(rig: Rig, scale: int = 4, path: str = '') -> dict:
    """The board's screen right now as a PNG (the OLED's pixels, the two-colour panel's tint).

    Args:
        scale: pixels per OLED pixel.
        path: where (default: the run folder).
    """
    out = Path(path) if path else rig.run_dir / 'screen.png'
    out.write_bytes(png_bytes(screen_rgb(rig.fb, max(1, min(16, scale)))))
    return {'png': str(out), 'state': rig.state.get('state'), 't': rig.state.get('t')}


@command('view.shot', group='view', slow=True)
def view_shot(rig: Rig, width: int = 1600, height: int = 1000, tab: str = '', path: str = '') -> dict:
    """A picture of the viewer (the 3D board, the screen, the plots, the panels) rendered offscreen: no window opens.

    Args:
        width: pixels.
        height: pixels.
        tab: tune, gestures, settings or session.
        path: where (default: the run folder).
    """
    exe = RIG_DIR / 'view' / 'build' / 'slrig-view'
    if not exe.exists():
        raise RigError('view.missing', 'the viewer is not built', 'cmake -S rig/view -B rig/view/build -G Ninja && cmake --build rig/view/build')
    out = Path(path) if path else rig.run_dir / 'view.png'
    cmd = [str(exe), '--shot', str(out), '--size', f'{width}x{height}', '--wait-ms', '1500',
           '--socket', str(rig.socket_path)]
    if tab:
        cmd += ['--tab', tab]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if r.returncode != 0 or not out.exists():
        raise RigError('view.failed', (r.stderr or r.stdout or 'no picture')[-400:], '')
    return {'png': str(out)}


@command('curve', group='view')
def curve(rig: Rig, n: int = 121) -> dict:
    """The tilt-to-speed curve and the roll-to-effects curves as the firmware's own gesture.c computes them with the
    board's current tuning and Speed min/max (the twin's calculator).

    Args:
        n: points from -90 to +90 degrees.
    """
    calc = _calc(rig)
    calc.load_tune(rig.tune_listing or [])
    smin = _setting_value(rig, 'Speed min', 0.05)
    smax = _setting_value(rig, 'Speed max', 2.0)
    r = calc.ask(f'curve {int(n)} {smin} {smax}')
    try:
        out = json.loads(r)
    except ValueError:
        raise RigError('curve.failed', r[:200], '')
    out['spd_min'], out['spd_max'] = smin, smax
    return out


GESTURES = [
    ('tilt.zone', 'tilt', 'the lean leaves the dead zone (±{tilt.dead}°; +{tilt.hyst}° to leave it): forward (nose down) or backward', ['tilt.dead', 'tilt.hyst', 'tilt.full', 'tilt.sign', 'tilt.axis', 'tilt.curve']),
    ('tilt.detent', 'tilt', 'the speed sticks to a detent ({tilt.detents}) within {tilt.detent_w} octaves', ['tilt.detents', 'tilt.detent_w']),
    ('roll.delay', 'roll', 'roll left past {roll.dead}°: the delay (full at {roll.full}°)', ['roll.on', 'roll.dead', 'roll.full', 'roll.sign']),
    ('roll.stutter', 'roll', 'roll right past {roll.dead}°: the stutter (full at {roll.full}°)', ['roll.on', 'roll.dead', 'roll.full', 'roll.sign']),
    ('roll.off', 'roll', 'back inside the roll dead zone', ['roll.dead']),
    ('freeze.on', 'motion', 'the knob held while playing (or MIDI note 64): the motion stops counting', ['enc.hold']),
    ('freeze.off', 'motion', 'the same again: the motion counts again', ['enc.hold']),
    ('enc.turn', 'enc', 'every {enc.steps} counts is a detent (direction {enc.dir}, glitches under {enc.glitch} ns ignored, acceleration {enc.accel})', ['enc.steps', 'enc.dir', 'enc.glitch', 'enc.accel']),
    ('enc.down', 'enc', 'the knob goes down', []),
    ('enc.up', 'enc', 'the knob comes up (ms = how long it was down)', []),
    ('enc.tap', 'enc', 'let go after {enc.tap_min}..{enc.hold} ms: menu, dive, back', ['enc.tap_min', 'enc.hold']),
    ('enc.double', 'enc', 'a second tap within {enc.double} ms: the knob on the Volume for {enc.vol_ms} ms', ['enc.double', 'enc.vol_ms']),
    ('enc.hold', 'enc', 'let go after {enc.hold} ms or more: leave the menu, or freeze in play', ['enc.hold']),
    ('enc.long', 'enc', 'held {enc.long} ms', ['enc.long']),
    ('enc.bounce', 'enc', 'let go before {enc.tap_min} ms: contact bounce, ignored', ['enc.tap_min']),
    ('menu.open', 'enc', 'a tap with no second tap within {enc.double} ms', ['enc.double']),
    ('menu.close', 'enc', 'a hold in the menu', ['enc.hold']),
    ('rec.down', 'rec', 'REC (or BOOT, or note 60) down for {rec.debounce} ticks of 10 ms', ['rec.debounce']),
    ('rec.up', 'rec', 'REC up for {rec.debounce} ticks', ['rec.debounce']),
    ('take.state', 'rec', 'the recorder: idle, rec, play', []),
    ('joy.flick', 'joy', 'the stick past {joy.flick} of its travel; ready again inside {joy.rearm} (joystick {joy.on})', ['joy.on', 'joy.flick', 'joy.rearm', 'joy.dead']),
    ('joy.centre', 'joy', "the stick's rest position taken", ['joy.centre']),
    ('joy.pinned', 'joy', 'the stick sat off centre for 8 s: ignored until re-centred', []),
]


@command('gestures', group='view')
def gestures(rig: Rig) -> list:
    """Every gesture the board knows: its rule in words with the current numbers, the tuning values behind it, how
    often it fired since the rig connected and when last."""
    vals = {it['name']: it for it in rig.tune.values()}

    def fmt(rule: str) -> str:
        import re
        def sub(m):
            it = vals.get(m.group(1))
            if not it:
                return m.group(1)
            if it['kind'] in ('enum', 'bool') and it['labels']:
                k = int(it['value'] - it['min'])
                return it['labels'][k] if 0 <= k < len(it['labels']) else f"{it['value']:g}"
            return f"{it['value']:g}"
        return re.sub(r'\{([a-z_.]+)\}', sub, rule)
    return [{'name': n, 'group': g, 'rule': fmt(r), 'tune': t, 'count': rig.counts.get(n, 0), 'last_t': rig.last_seen.get(n)}
            for n, g, r, t in GESTURES]


# ---------------------------------------------------------------- audio
def _pw_nodes() -> list[dict]:
    try:
        r = subprocess.run(['pw-dump'], capture_output=True, text=True, timeout=5)
        objs = json.loads(r.stdout)
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return []
    out = []
    for o in objs:
        props = (o.get('info') or {}).get('props') or {}
        if o.get('type', '').endswith('Node') and props.get('media.class', '').startswith('Audio/'):
            out.append({'id': o.get('id'), 'name': props.get('node.name', ''), 'desc': props.get('node.description', ''),
                        'class': props.get('media.class', '')})
    return out


@command('audio.devices', group='audio')
def audio_devices(rig: Rig) -> list:
    """The board's PipeWire nodes (its USB sound card: what it plays to the computer, what the computer plays in)."""
    return [n for n in _pw_nodes() if 'samplooler' in (n['name'] + n['desc']).lower()]


@command('audio.measure', group='audio', slow=True)
def audio_measure(rig: Rig, secs: float = 3.0, tone_hz: float = 0.0) -> dict:
    """Listen to the board over USB for a while and measure: levels, noise floor, clicks, bad samples, and with a
    known tone in the loop, the real playback speed over time. On the twin, its own output is measured.

    Args:
        secs: how long.
        tone_hz: the frequency of the tone recorded as the take (0: no speed measurement).
    """
    from . import audio_analysis as A
    _need_port(rig)
    out = rig.run_dir / f"audio-{time.strftime('%H%M%S')}.wav"
    if rig.source == 'twin':
        tw = rig.run_dir / 'twin.wav'
        if not tw.exists():
            raise RigError('audio.twin', 'the twin is not writing its sound', 'connect source=twin again')
        time.sleep(secs)
        data = tw.read_bytes()[44:]
        n = int(secs * 32000) * 2
        pcm = data[-n:] if len(data) >= n else data
        import wave
        with wave.open(str(out), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(32000); w.writeframes(pcm)
    else:
        src = [n for n in audio_devices(rig) if n['class'] == 'Audio/Source']
        if not src:
            raise RigError('audio.none', "the board's USB sound card is not in PipeWire", 'audio.devices')
        p = subprocess.Popen(['pw-record', '--target', src[0]['name'], '--rate', '32000', '--channels', '2',
                              '--format', 's16', str(out)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        time.sleep(secs + 0.3)
        p.terminate()
        try:
            p.wait(2)
        except subprocess.TimeoutExpired:
            p.kill()
    x, sr = A.read_wav(str(out))
    chans = x if x.ndim == 2 else x[:, None]
    res = {'wav': str(out), 'secs': round(len(chans) / sr, 2), 'channels': []}
    for c in range(chans.shape[1]):
        ch = {'levels': A.levels(chans[:, c], sr), 'clicks': A.clicks(chans[:, c], sr), 'bad': A.bad_samples(chans[:, c])}
        if tone_hz and c == 0:
            ch['speed'] = A.speed_from_tone(chans[:, c], sr, tone_hz)
        res['channels'].append(ch)
    return res


# ---------------------------------------------------------------- the rest
@command('osc.config', group='osc')
def osc_config(rig: Rig, out_host: str = '', out_port: int = 0, in_port: int = 0, on: bool | None = None) -> dict:
    """Where OSC goes and listens (slrig/osc.py lists the addresses). On by default: 127.0.0.1:9000 out, 9001 in.

    Args:
        out_host: the host to send to.
        out_port: the port to send to.
        in_port: the port to listen on.
        on: send or not.
    """
    return rig.osc.configure(out_host or None, out_port or None, in_port or None, on)


@command('log.tail', group='log')
def log_tail(rig: Rig, n: int = 30, level: str = '') -> list:
    """The board's log lines (and the rig's notices), newest last.

    Args:
        n: how many.
        level: warn: only warnings.
    """
    items = [l for l in rig.logs if not level or l['level'] == level]
    return items[-n:]


@command('events.tail', group='log')
def events_tail(rig: Rig, n: int = 30, name: str = '') -> list:
    """The gestures that fired, newest last.

    Args:
        n: how many.
        name: only these (a prefix: enc, tilt, roll ...).
    """
    items = [e for e in rig.events if not name or e['name'].startswith(name)]
    return items[-n:]


@command('cmds.tail', group='log')
def cmds_tail(rig: Rig, n: int = 30) -> list:
    """The commands anyone ran (viewer, CLI, OSC, MCP), newest last.

    Args:
        n: how many.
    """
    return list(rig.cmds)[-n:]


@command('motion.snapshot', group='view')
def motion_snapshot(rig: Rig, secs: float = 1.0) -> dict:
    """The motion of the last moments in numbers: the latest sample, means and spreads over the window (device axes),
    the board's angles and the host's reference orientation.

    Args:
        secs: the window.
    """
    with rig.lock:
        n = min(rig.m_n, len(rig.m_t))
        if n == 0:
            return {'samples': 0}
        idx = (np.arange(n) + rig.m_n - n) % len(rig.m_t)
        t, acc, gyro = rig.m_t[idx], rig.m_acc[idx], rig.m_gyro[idx]
        sel = t >= t[-1] - secs
        acc, gyro = acc[sel], gyro[sel]
        return {'samples': int(sel.sum()), 'rate_hz': rig.motion_rate, 'latest': {'acc': acc[-1].round(4).tolist(), 'gyro': gyro[-1].round(3).tolist()},
                'acc_mean': acc.mean(0).round(4).tolist(), 'acc_std': acc.std(0).round(5).tolist(),
                'gyro_mean': gyro.mean(0).round(3).tolist(), 'gyro_std': gyro.std(0).round(4).tolist(),
                '|a|': round(float(np.linalg.norm(acc, axis=1).mean()), 4),
                'board': {k: rig.state.get(k) for k in ('pitch', 'roll', 'yaw', 'quat', 'bias', 'rest')},
                'ref_quat': [round(float(x), 5) for x in rig.ref.q]}


@command('wait', group='util', slow=True)
def wait(rig: Rig, secs: float = 1.0) -> dict:
    """Do nothing for a while (between steps of a batch).

    Args:
        secs: how long.
    """
    time.sleep(max(0.0, min(secs, 600.0)))
    return {'waited': secs}


@command('help', group='util')
def help_(rig: Rig, topic: str = '') -> dict:
    """The commands: with no topic, every name and its one-line summary by group; with a command's name, its arguments
    as JSON Schema and examples; with a group's name, its commands.

    Args:
        topic: a command, a group, or nothing.
    """
    if topic and topic in REGISTRY:
        return get(topic).schema()
    groups: dict[str, list] = {}
    for c in REGISTRY.values():
        if not topic or c.group == topic:
            groups.setdefault(c.group, []).append({'name': c.name, 'summary': c.summary.split('. ')[0]})
    return groups
