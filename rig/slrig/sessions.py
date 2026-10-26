"""Recordings on disk (rig/PROTOCOL.md, section 3): a folder per session, plain files, the board's clock."""
from __future__ import annotations

import json
import os
import struct
import time
from datetime import datetime
from pathlib import Path

import numpy as np

RIG = Path(__file__).resolve().parent.parent


def sessions_dir() -> Path:
    d = os.environ.get('SLRIG_SESSIONS')
    p = Path(d).expanduser() if d else RIG.parent.parent / 'lab' / 'rig' / 'sessions'
    p.mkdir(parents=True, exist_ok=True)
    return p


def _slug(s: str) -> str:
    out = ''.join(c if c.isalnum() or c in '-_' else '-' for c in s.strip().lower())
    return '-'.join(x for x in out.split('-') if x)[:48] or 'take'


class SessionWriter:
    """Everything that arrives while recording, as it arrives. t is seconds from the first message on the board's clock."""

    def __init__(self, name: str, meta: dict, fields: list[str]):
        stamp = datetime.now().strftime('%Y-%m-%d-%H%M%S')
        self.name = f'{stamp}-{_slug(name)}'
        self.path = sessions_dir() / self.name
        self.path.mkdir(parents=True, exist_ok=False)
        self.meta = dict(meta, name=self.name, started=datetime.now().isoformat(timespec='seconds'))
        self.fields = list(fields)
        self.t0: float | None = None
        self.t_last = 0.0
        self.n_motion = self.n_state = self.n_events = self.n_screens = 0
        self.f_motion = open(self.path / 'motion.csv', 'w')
        self.f_motion.write('t,ax,ay,az,gx,gy,gz,flags\n')
        self.f_raw = open(self.path / 'raw.txt', 'w')
        self.f_state = open(self.path / 'state.csv', 'w')
        self.f_state.write('t,' + ','.join(self.fields) + '\n')
        self.f_events = open(self.path / 'events.jsonl', 'w')
        self.f_screen = open(self.path / 'screen.bin', 'wb')
        self.wall0 = time.monotonic()
        self.closed = False
        self._write_meta()

    def _t(self, tb: float) -> float:
        if self.t0 is None:
            self.t0 = tb
        t = tb - self.t0
        self.t_last = max(self.t_last, t)
        return t

    def motion(self, tb: np.ndarray, acc: np.ndarray, gyro: np.ndarray, raw: np.ndarray, flags: int) -> None:
        if self.closed or len(tb) == 0:
            return
        t = np.array([self._t(float(x)) for x in tb])
        block = np.column_stack([t, acc, gyro, np.full(len(t), flags)])
        np.savetxt(self.f_motion, block, fmt=['%.5f', '%.5f', '%.5f', '%.5f', '%.3f', '%.3f', '%.3f', '%d'], delimiter=',')
        np.savetxt(self.f_raw, raw, fmt='%d')
        self.n_motion += len(t)

    def state(self, tb: float, row: dict) -> None:
        if self.closed:
            return
        t = self._t(tb)
        vals = [row.get(f, 0) for f in self.fields]
        self.f_state.write(f'{t:.4f},' + ','.join(f'{v:.5g}' if isinstance(v, float) else str(int(v)) for v in vals) + '\n')
        self.n_state += 1

    def event(self, tb: float, ev: dict) -> None:
        if self.closed:
            return
        self.f_events.write(json.dumps(dict(ev, t=round(self._t(tb), 4))) + '\n')
        self.n_events += 1

    def screen(self, tb: float, fb: bytes) -> None:
        if self.closed or len(fb) != 1024:
            return
        self.f_screen.write(struct.pack('<d', self._t(tb)) + bytes(fb))
        self.n_screens += 1

    def _write_meta(self) -> None:
        (self.path / 'meta.json').write_text(json.dumps(self.meta, indent=1))

    def close(self, **more) -> dict:
        if self.closed:
            return self.summary()
        self.closed = True
        for f in (self.f_motion, self.f_raw, self.f_state, self.f_events, self.f_screen):
            f.close()
        self.meta.update(more)
        self.meta['secs'] = round(self.t_last, 3)
        self.meta['counts'] = {'motion': self.n_motion, 'state': self.n_state, 'events': self.n_events, 'screens': self.n_screens}
        self._write_meta()
        return self.summary()

    def summary(self) -> dict:
        return {'name': self.name, 'path': str(self.path), 'secs': round(self.t_last, 3),
                'motion': self.n_motion, 'state': self.n_state, 'events': self.n_events, 'screens': self.n_screens}


def list_sessions(limit: int = 50) -> list[dict]:
    out = []
    for p in sorted(sessions_dir().iterdir(), reverse=True):
        if not (p / 'meta.json').exists():
            continue
        try:
            m = json.loads((p / 'meta.json').read_text())
        except (OSError, ValueError):
            continue
        out.append({'name': p.name, 'started': m.get('started'), 'secs': m.get('secs'), 'source': m.get('source'),
                    'test': (m.get('test') or {}).get('name'), 'notes': m.get('notes', ''),
                    'analysed': (p / 'analysis.json').exists()})
        if len(out) >= limit:
            break
    return out


def resolve(name: str | None) -> Path:
    """A session by its full name, a unique part of it, or 'last' / None for the newest."""
    d = sessions_dir()
    names = sorted((p.name for p in d.iterdir() if (p / 'meta.json').exists()), reverse=True)
    if not names:
        raise FileNotFoundError('no sessions yet')
    if not name or name == 'last':
        return d / names[0]
    if (d / name).exists():
        return d / name
    hits = [n for n in names if name in n]
    if len(hits) == 1 or (hits and all(h.endswith(name) for h in hits[:1])):
        return d / hits[0]
    if not hits:
        raise FileNotFoundError(f'no session matching {name!r}')
    raise FileNotFoundError(f'{len(hits)} sessions match {name!r}: ' + ', '.join(hits[:5]))
