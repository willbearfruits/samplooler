"""The rig's model of the board: one port (a Samplooler or the twin), everything it says decoded and kept, pushed to the
clients that asked (rig/PROTOCOL.md, section 2), recorded when a session runs. All commands go through Rig.run()."""
from __future__ import annotations

import json
import queue
import threading
import time
from collections import Counter, deque
from pathlib import Path
from typing import Any

import numpy as np

from . import port as portmod
from . import wire
from .osc import Osc
from .registry import REGISTRY, RigError, bind, get
from .sessions import SessionWriter

FLAG_BITS = {'frozen': 1, 'menu': 2, 'injected': 4, 'rec_down': 8, 'enc_down': 16, 'joy_present': 32, 'imu_present': 64,
             'recording': 128, 'playing': 256, 'joy_override': 512, 'rest': 1024, 'menu_edit': 2048}
DEFAULT_FIELDS = ['state', 'mode', 'flags', 'qw', 'qx', 'qy', 'qz', 'pitch', 'roll', 'yaw', 'tilt_u', 'zone', 'detent',
                  'speed_cmd', 'speed_now', 'pos', 'loop_s', 'delay', 'stutter', 'joy_x', 'joy_y', 'joy_rx', 'joy_ry',
                  'enc', 'in_peak', 'out_peak', 'in_clips', 'cpu', 'heap_kb', 'i2c_fail', 'bias_x', 'bias_y', 'bias_z',
                  'rate_hz', 'menu_param', 'loop_ms', 'wire_drops']
RING = 60 * 1000                             # 60 s of motion at 1 kHz


def device_axes(chip: np.ndarray, axis_map: tuple[int, int, int]) -> np.ndarray:
    """Chip axes to device axes (tune.h motion.ax_*: 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z), rows of 3."""
    out = np.empty_like(chip, dtype=np.float64)
    for k, e in enumerate(axis_map):
        e = e if 0 <= e <= 5 else 2 * k
        out[:, k] = -chip[:, e >> 1] if e & 1 else chip[:, e >> 1]
    return out


class RefFusion:
    """The host's own estimate of the orientation (Mahony, kp 2, no integral) from the raw stream: a second opinion
    beside the board's fusion, drawn as the viewer's ghost."""

    def __init__(self):
        self.q = np.array([1.0, 0, 0, 0])
        self.started = False

    def feed(self, acc: np.ndarray, gyro: np.ndarray, dt: float, bias: np.ndarray) -> None:
        q = self.q
        for a, g in zip(acc, gyro - bias):
            n = np.sqrt(a @ a)
            if not self.started and n > 0.05:
                u = a / n
                q = np.array([1 + u[2], u[1], -u[0], 0.0])
                q /= np.sqrt(q @ q)
                self.started = True
            w = np.radians(g)
            if n > 0.05 and abs(n - 1) < 0.2:
                ax, ay, az = a / n
                vx = 2 * (q[1] * q[3] - q[0] * q[2]); vy = 2 * (q[0] * q[1] + q[2] * q[3])
                vz = q[0] ** 2 - q[1] ** 2 - q[2] ** 2 + q[3] ** 2
                e = np.array([ay * vz - az * vy, az * vx - ax * vz, ax * vy - ay * vx])
                w = w + 2.0 * (1 - abs(n - 1) / 0.2) * e
            q0, q1, q2, q3 = q
            q = q + 0.5 * dt * np.array([-q1 * w[0] - q2 * w[1] - q3 * w[2], q0 * w[0] + q2 * w[2] - q3 * w[1],
                                         q0 * w[1] - q1 * w[2] + q3 * w[0], q0 * w[2] + q1 * w[1] - q2 * w[0]])
            q /= np.sqrt(q @ q)
        self.q = q


class Sub:
    """A client's subscription: the streams it wants and the queue its writer drains."""

    def __init__(self, client: str):
        self.client = client
        self.streams: set[str] = set()
        self.q: queue.Queue[str] = queue.Queue(maxsize=4000)

    def put(self, line: str, droppable: bool = True) -> None:
        try:
            self.q.put_nowait(line)
        except queue.Full:
            if not droppable:
                try:
                    self.q.get_nowait()
                    self.q.put_nowait(line)
                except (queue.Empty, queue.Full):
                    pass


class Rig:
    def __init__(self, run_dir: Path | None = None):
        self.lock = threading.RLock()
        self.run_dir = run_dir or Path('/tmp')
        self.port: portmod.Port | None = None
        self.parser = wire.Parser()
        self.source = 'none'
        self.board: dict = {}
        self.tune: dict[int, dict] = {}
        self.tune_listing: list[dict] | None = None
        self.tune_cv = threading.Condition(self.lock)
        self.settings: dict[int, dict] = {}
        self.settings_listing: list[dict] | None = None
        self.fields: list[tuple[str, float]] = [(f, 1.0) for f in DEFAULT_FIELDS]
        self.names = {'states': ['idle', 'rec', 'play'], 'modes': ['TAPE', 'GRAIN'], 'zones': ['dead', 'fwd', 'back']}
        self.state: dict[str, Any] = {}
        self.state_t = 0.0
        self.fb = bytearray(1024)
        self.fb_dirty = False
        self.want = {'motion': True, 'state': True, 'screen': True, 'events': True}
        # motion, last 60 s
        self.m_t = np.zeros(RING); self.m_acc = np.zeros((RING, 3)); self.m_gyro = np.zeros((RING, 3))
        self.m_raw = np.zeros((RING, 6), dtype=np.int16); self.m_n = 0
        self.motion_rate = 0; self.acc_range = 0; self.gyro_range = 2
        self.pending: list[tuple[float, float, np.ndarray, np.ndarray, int]] = []
        self.state_hist: deque = deque(maxlen=6000)
        self.events: deque = deque(maxlen=4000)
        self.logs: deque = deque(maxlen=4000)
        self.cmds: deque = deque(maxlen=500)
        self.counts: Counter = Counter()
        self.last_seen: dict[str, float] = {}
        self.seq = {'r': None, 'q': None}
        self.drops = Counter()
        self.rate_count = Counter()
        self.rates = {'motion_hz': 0.0, 'state_hz': 0.0, 'screen_fps': 0.0}
        self.subs: list[Sub] = []
        self.recorder: SessionWriter | None = None
        self.ref = RefFusion()
        self.ab: dict[str, dict | None] = {'A': None, 'B': None}
        self.calc: portmod.Calc | None = None
        self.osc = Osc(lambda c, a, by: self.run(c, a, by))
        self.info_t = 0.0
        self.closing = False
        self.busy: dict[str, Any] = {}          # a guided test or an injection in progress
        self.alive = True
        self.socket_path = Path('')
        threading.Thread(target=self._heartbeat, name='heartbeat', daemon=True).start()
        threading.Thread(target=self._pusher, name='pusher', daemon=True).start()

    # ------------------------------------------------------------ the port
    def connect(self, source: str = 'auto', serial: str = '', scene: str = 'still') -> dict:
        with self.lock:
            self.disconnect()
            found = portmod.boards()
            if serial:
                found = [b for b in found if serial.upper().replace(':', '') in b['serial'].upper().replace(':', '')]
            if source in ('auto', 'board') and found:
                if len(found) > 1 and not serial:
                    raise RigError('board.many', 'more than one Samplooler on USB: ' + ', '.join(b['serial'] for b in found),
                                   'connect with serial=...')
                b = found[0]
                try:
                    self.port = portmod.BoardPort(b, self._bytes, self._closed)
                except OSError as e:
                    raise RigError('board.busy', f"cannot open {b['dev']}: {e.strerror}",
                                   'is the desktop app, the plugin or sl_midi.py holding the port?')
                self.source = 'board'
            elif source == 'board':
                raise RigError('board.none', 'no Samplooler on USB', 'plug one in, or connect source=twin')
            else:
                self.port = portmod.TwinPort(self._bytes, self._closed, scene=scene, log_path=self.run_dir / 'twin.log',
                                             wav_path=self.run_dir / 'twin.wav')
                self.source = 'twin'
            self.board = dict(self.port.board)
            self.parser = wire.Parser()
            self.tune, self.settings, self.state = {}, {}, {}
            self.seq = {'r': None, 'q': None}
            self.ref = RefFusion()
            self._send(wire.info())
            self._send(wire.go(**self.want))
            self._send(wire.tune_list())
            self._send(wire.describe())
            self.info_t = time.monotonic()
        self.notice(f'connected to {self.board.get("name")} ({self.source})')
        self._wait(lambda: self.tune_listing is not None and len(self.tune) > 0, 3.0)
        self.push_status()
        return self.status()

    def disconnect(self) -> None:
        with self.lock:
            if self.port:
                try:
                    self._send(wire.go())
                except Exception:
                    pass
                self.port.close()
            self.port = None
            self.source = 'none'
            self.tune_listing = None

    def _closed(self, why: str) -> None:
        self.notice(why, 'warn')
        with self.lock:
            self.port = None
            self.source = 'none'
        self.push_status()

    def _send(self, data: bytes) -> None:
        if not self.port:
            raise RigError('board.none', 'nothing connected', 'run connect')
        self.port.write(data)

    def _wait(self, cond, secs: float) -> bool:
        end = time.monotonic() + secs
        with self.tune_cv:
            while not cond():
                left = end - time.monotonic()
                if left <= 0:
                    return False
                self.tune_cv.wait(left)
        return True

    # ------------------------------------------------------------ what the board says
    def _bytes(self, data: bytes) -> None:
        msgs = self.parser.feed(data)
        with self.lock:
            for m in msgs:
                try:
                    if m[0] == 'sysex':
                        self._sysex(m[1])
                except (ValueError, IndexError, KeyError) as e:
                    self.drops['bad'] += 1
                    self.notice(f'a message the rig could not read: {e}', 'warn')

    def _board_t(self, frames: int) -> float:
        return frames / wire.SR

    def _gap(self, kind: str, seq: int) -> None:
        last = self.seq.get(kind)
        if last is not None:
            missed = (seq - last - 1) % 128
            if missed:
                self.drops[kind] += missed
        self.seq[kind] = seq

    def _sysex(self, body: bytes) -> None:
        if len(body) < 2 or body[0] != 0x7D:
            return
        cmd, p = chr(body[1]), body[2:]
        if cmd == 'r':
            self._motion(wire.decode_r(p))
        elif cmd == 'q':
            self._state(wire.decode_q(p, self.fields))
        elif cmd == 'Q':
            q = wire.decode_Q(p)
            if q['fields']:
                self.fields = q['fields']
            self.names.update({k: q[k] for k in ('states', 'modes', 'zones')})
        elif cmd == 'o':
            page, drops, data = wire.decode_o(p)
            self.fb[128 * page:128 * page + 128] = data
            self.fb_dirty = True
            self.rate_count['screen'] += 1
            if self.recorder:
                self.recorder.screen(self.state_t, self.fb)
        elif cmd == 'e':
            kv = wire.text_kv(p)
            ev = {'t': round(self.state_t, 4), 'name': kv.get('n', '?'), 'v': float(kv.get('v', 0) or 0),
                  'ms': int(float(kv.get('ms', 0) or 0)), 'text': kv.get('x', '')}
            self.events.append(ev)
            self.counts[ev['name']] += 1
            self.last_seen[ev['name']] = ev['t']
            if self.recorder:
                self.recorder.event(self.state_t, ev)
            self.push('event', ev)
            self.osc.send('/samplooler/event', ev['name'], float(ev['v']), ev['ms'], ev['text'])
        elif cmd == 't':
            self._tune_line(wire.text_kv(p))
        elif cmd == 'I':
            kv = wire.text_kv(p)
            self.board.update({k: kv[k] for k in ('name', 'version', 'serial', 'proto', 'rig') if k in kv})
        elif cmd == 'D':
            kv = wire.text_kv(p)
            if 'end' in kv:
                self.settings_listing = [self.settings[i] for i in sorted(self.settings)]
                self.push('settings', {'items': self.settings_listing})
            elif 'i' in kv:
                i = int(kv['i'])
                self.settings[i] = {'i': i, 'tag': kv.get('tag', f'p{i:02d}'), 'name': kv.get('name', ''),
                                    'group': kv.get('group', 'SETTINGS'), 'kind': kv.get('kind', 'step'),
                                    'text': kv.get('text', ''), 'help': kv.get('help', '')}
                if 'confirm' in kv:
                    self.settings[i]['confirm'] = kv['confirm']
        elif cmd == 'V':
            kv = wire.text_kv(p)
            tag = kv.get('tag', '')
            for s in self.settings.values():
                if s['tag'] == tag:
                    s['text'] = kv.get('text', s['text'])
            self.settings_listing = [self.settings[i] for i in sorted(self.settings)]
            self.push('settings', {'items': self.settings_listing})
        elif cmd == 'L':
            text = p.decode('ascii', 'replace')
            level = 'warn' if any(w in text for w in ('W (', 'fail', 'FAIL', 'stuck', 'CRASH')) else 'info'
            self.logs.append({'t': time.time(), 'text': text, 'level': level})
            self.push('log', {'text': text, 'level': level})
        self.tune_cv.notify_all()

    def axis_map(self) -> tuple[int, int, int]:
        def v(n, d):
            i = self.tune_index(n)
            return int(self.tune[i]['value']) if i is not None else d
        return v('motion.ax_x', 3), v('motion.ax_y', 1), v('motion.ax_z', 5)

    def _motion(self, r: dict) -> None:
        raw = r['raw']
        n = len(raw)
        self._gap('r', r['seq'])
        if n == 0:
            return
        self.motion_rate, self.acc_range, self.gyro_range = r['rate_hz'], r['acc_range'], r['gyro_range']
        al, gl = wire.ACC_LSB[r['acc_range']], wire.GYRO_LSB[r['gyro_range']]
        amap = self.axis_map()
        acc = device_axes(raw[:, :3].astype(np.float64) / al, amap)
        gyro = device_axes(raw[:, 3:].astype(np.float64) / gl, amap)
        t0 = self._board_t(r['frames'])
        t = t0 + np.arange(n) / r['rate_hz']
        k = self.m_n % RING
        idx = (np.arange(n) + k) % RING
        self.m_t[idx], self.m_acc[idx], self.m_gyro[idx], self.m_raw[idx] = t, acc, gyro, raw
        self.m_n += n
        self.rate_count['motion'] += n
        bias = device_axes(np.array([[self.state.get('bias_x', 0), self.state.get('bias_y', 0), self.state.get('bias_z', 0)]]), amap)[0]
        self.ref.feed(acc, gyro, 1.0 / r['rate_hz'], bias)
        self.pending.append((t0, 1.0 / r['rate_hz'], acc, gyro, r['flags']))
        if self.recorder:
            self.recorder.motion(t, acc, gyro, raw, r['flags'])

    def _state(self, s: dict) -> None:
        self._gap('q', s.pop('seq'))
        frames = s.pop('frames')
        t = self._board_t(frames)
        fl = int(s.get('flags', 0))
        st = dict(s)
        st['t'] = round(t, 4)
        for k, bit in FLAG_BITS.items():
            st[k] = bool(fl & bit)
        si, mi, zi = int(s.get('state', 0)), int(s.get('mode', 0)), int(s.get('zone', 0))
        st['state'] = self.names['states'][si] if 0 <= si < len(self.names['states']) else str(si)
        st['mode'] = self.names['modes'][mi] if 0 <= mi < len(self.names['modes']) else str(mi)
        st['zone'] = 'detent' if s.get('detent', 0) > 0 else (self.names['zones'][zi] if 0 <= zi < len(self.names['zones']) else str(zi))
        st['quat'] = [s.get('qw', 1), s.get('qx', 0), s.get('qy', 0), s.get('qz', 0)]
        st['bias'] = [s.get('bias_x', 0), s.get('bias_y', 0), s.get('bias_z', 0)]
        self.state, self.state_t = st, t
        self.state_hist.append(st)
        self.rate_count['state'] += 1
        if self.recorder:
            self.recorder.state(t, s | {'state': si, 'mode': mi, 'zone': zi})
        self.push('state', st)
        o = self.osc
        if o.on:
            o.send('/samplooler/angles', float(st.get('pitch', 0)), float(st.get('roll', 0)), float(st.get('yaw', 0)))
            o.send('/samplooler/quat', *[float(x) for x in st['quat']])
            o.send('/samplooler/speed', float(st.get('speed_cmd', 0)), float(st.get('speed_now', 0)))
            o.send('/samplooler/fx', float(st.get('delay', 0)), float(st.get('stutter', 0)))
            o.send('/samplooler/state', st['state'], st['mode'])

    def _tune_line(self, kv: dict) -> None:
        if 'end' in kv:
            self.tune_listing = [self.tune[i] for i in sorted(self.tune)]
            self.push('tune', {'items': self.tune_listing})
            return
        if 'saved' in kv:
            self.notice('tuning saved on the board' if kv['saved'] == '1' else 'the board could not save the tuning', 'info' if kv['saved'] == '1' else 'warn')
            return
        if 'n' in kv:
            it = wire.tune_item(kv)
            self.tune[it['i']] = it
        elif 'i' in kv and 'v' in kv:
            i = int(kv['i'])
            if i in self.tune:
                self.tune[i]['value'] = float(kv['v'])
                self.tune_listing = [self.tune[j] for j in sorted(self.tune)]
                self.push('tune', {'items': self.tune_listing})

    def tune_index(self, name: str) -> int | None:
        for i, it in self.tune.items():
            if it['name'] == name:
                return i
        return None

    # ------------------------------------------------------------ out to the clients
    def subscribe(self, sub: Sub, streams: list[str]) -> None:
        with self.lock:
            sub.streams |= set(streams)
            if sub not in self.subs:
                self.subs.append(sub)
        snap = []
        if 'tune' in streams and self.tune_listing:
            snap.append(('tune', {'items': self.tune_listing}))
        if 'settings' in streams and self.settings_listing:
            snap.append(('settings', {'items': self.settings_listing}))
        if 'screen' in streams:
            snap.append(('screen', self._screen_payload()))
        if 'status' in streams:
            snap.append(('status', self.status()))
        for ev, payload in snap:
            sub.put(json.dumps(dict(payload, ev=ev)), droppable=False)

    def unsubscribe(self, sub: Sub, streams: list[str] | None = None) -> None:
        with self.lock:
            if streams is None:
                if sub in self.subs:
                    self.subs.remove(sub)
            else:
                sub.streams -= set(streams)

    def push(self, ev: str, payload: dict) -> None:
        if not self.subs:
            return
        line = None
        for s in list(self.subs):
            if ev in s.streams:
                line = line or json.dumps(dict(payload, ev=ev))
                s.put(line, droppable=ev in ('motion', 'state', 'screen'))

    def notice(self, text: str, level: str = 'info') -> None:
        self.logs.append({'t': time.time(), 'text': 'rig: ' + text, 'level': level})
        self.push('log', {'text': 'rig: ' + text, 'level': level})

    def _screen_payload(self) -> dict:
        import base64
        return {'w': 128, 'h': 64, 'fb': base64.b64encode(bytes(self.fb)).decode()}

    def _pusher(self) -> None:
        """At most 60 motion pushes a second (each with every sample since the last), screens on change, rates."""
        last_rate = time.monotonic()
        last_osc = 0.0
        while self.alive:
            time.sleep(1 / 60)
            with self.lock:
                pend, self.pending = self.pending, []
                dirty, self.fb_dirty = self.fb_dirty, False
                q = [float(x) for x in self.ref.q]
            if pend:
                acc = np.concatenate([p[2] for p in pend]); gyro = np.concatenate([p[3] for p in pend])
                self.push('motion', {'t0': round(pend[0][0], 5), 'dt': pend[0][1], 'acc': np.round(acc, 4).tolist(),
                                     'gyro': np.round(gyro, 3).tolist(), 'raw': True, 'ref_quat': q,
                                     'injected': bool(pend[-1][4] & 1)})
                now = time.monotonic()
                if now - last_osc > 0.01:
                    last_osc = now
                    self.osc.send('/samplooler/motion', *[float(x) for x in acc[-1]], *[float(x) for x in gyro[-1]])
            if dirty:
                self.push('screen', self._screen_payload())
            now = time.monotonic()
            if now - last_rate >= 1.0:
                with self.lock:
                    dt = now - last_rate
                    self.rates = {'motion_hz': round(self.rate_count['motion'] / dt, 1),
                                  'state_hz': round(self.rate_count['state'] / dt, 1),
                                  'screen_fps': round(self.rate_count['screen'] / dt / 8, 1)}
                    self.rate_count.clear()
                last_rate = now
                self.push_status()

    def _heartbeat(self) -> None:
        """The board's streams stop 3 s after the last G; the app line goes quiet 5 s after the last I."""
        while self.alive:
            time.sleep(1.0)
            with self.lock:
                if not self.port:
                    continue
                try:
                    self._send(wire.go(**self.want))
                    if time.monotonic() - self.info_t > 2.0:
                        self._send(wire.info())
                        self.info_t = time.monotonic()
                except (RigError, OSError, TimeoutError):
                    pass

    # ------------------------------------------------------------ status
    def status(self) -> dict:
        with self.lock:
            busy = {k: v for k, v in self.busy.items() if not k.startswith('_')}     # no threads or events on the wire
            return {'source': self.source, 'board': self.board, 'rates': dict(self.rates),
                    'drops': {k: int(v) for k, v in self.drops.items()},
                    'recording': self.recorder.summary() if self.recorder else None,
                    'osc': self.osc.info(), 'busy': busy or None,
                    'streams': self.want, 'imu': {'rate_hz': self.motion_rate, 'acc_range_g': 2 << self.acc_range,
                                                  'gyro_range_dps': 250 << self.gyro_range}}

    def push_status(self) -> None:
        self.push('status', self.status())

    # ------------------------------------------------------------ commands
    def run(self, name: str, args: dict | None = None, by: str = 'cli') -> dict:
        t0 = time.monotonic()
        try:
            cmd = get(name)
            kw = bind(cmd, args or {})
            result = cmd.fn(self, **kw)
            env = {'ok': True, 'result': result}
        except RigError as e:
            env = {'ok': False, 'error': {'code': e.code, 'message': e.message, 'hint': e.hint}}
        except FileNotFoundError as e:
            env = {'ok': False, 'error': {'code': 'not_found', 'message': str(e), 'hint': ''}}
        except Exception as e:                  # a bug: say so, keep serving
            import traceback
            env = {'ok': False, 'error': {'code': 'internal', 'message': f'{type(e).__name__}: {e}',
                                          'hint': traceback.format_exc(limit=3)[-600:]}}
        ms = round((time.monotonic() - t0) * 1000, 1)
        env['ms'] = ms
        if name not in ('status', 'log.tail', 'events.tail', 'help'):
            rec = {'by': by, 'cmd': name, 'args': args or {}, 'ok': env['ok'], 'ms': ms, 't': time.time()}
            if not env['ok']:
                rec['error'] = env['error']['message']
            self.cmds.append(rec)
            self.push('cmds', rec)
        return env

    def commands(self) -> list[str]:
        return sorted(REGISTRY)
