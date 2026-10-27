"""The board's SysEx (rig/PROTOCOL.md, section 1): framing, decoding and encoding. Pure: no ports, no threads.
The packing mirrors firmware/main/rig_codec.h; tests/test_wire.py checks the two against each other through the twin."""
from __future__ import annotations

import numpy as np

RATES = (100, 250, 500, 1000)
ACC_LSB = (16384.0, 8192.0, 4096.0, 2048.0)
GYRO_LSB = (131.0, 65.5, 32.8, 16.4)
SR = 32000                                   # audio frames a second: the board's clock


class Parser:
    """Bytes in, whole messages out: ('sysex', body) with body = the bytes between F0 and F7, or ('midi', st, d1, d2).
    Real-time bytes are skipped; another status byte inside a SysEx drops it (as the board does)."""

    def __init__(self):
        self.sx: bytearray | None = None
        self.st = 0
        self.d: list[int] = []

    def feed(self, data: bytes) -> list[tuple]:
        out = []
        for b in data:
            if b >= 0xF8:
                continue
            if b == 0xF0:
                self.sx = bytearray()
                continue
            if b == 0xF7:
                if self.sx is not None:
                    out.append(('sysex', bytes(self.sx)))
                self.sx = None
                continue
            if self.sx is not None:
                if b & 0x80:
                    self.sx = None
                else:
                    self.sx.append(b)
                    continue
            if b & 0x80:
                self.st, self.d = b, []
                continue
            if not self.st:
                continue
            kind = self.st & 0xF0
            need = 1 if kind in (0xC0, 0xD0) else 2
            self.d.append(b)
            if len(self.d) == need:
                out.append(('midi', self.st, self.d[0], self.d[1] if need == 2 else 0))
                self.d = []
        return out


# ---------------------------------------------------------------- packing (rig_codec.h)
def get16(p: bytes, i: int = 0) -> int:
    u = (p[i] & 3) << 14 | (p[i + 1] & 0x7F) << 7 | (p[i + 2] & 0x7F)
    return u - 65536 if u >= 32768 else u


def put16(v: int) -> bytes:
    u = int(v) & 0xFFFF
    return bytes(((u >> 14) & 3, (u >> 7) & 0x7F, u & 0x7F))


def get14(p: bytes, i: int = 0) -> int:
    u = (p[i] & 0x7F) << 7 | (p[i + 1] & 0x7F)
    return u - 16384 if u >= 8192 else u


def get32(p: bytes, i: int = 0) -> int:
    v = 0
    for k in range(5):
        v = (v << 7) | (p[i + k] & 0x7F)
    return v & 0xFFFFFFFF


def unpack7(src: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(src), 8):
        group = src[i:i + 8]
        hi = group[0]
        for j, b in enumerate(group[1:]):
            out.append(b | (((hi >> j) & 1) << 7))
    return bytes(out)


def text_kv(payload: bytes) -> dict[str, str]:
    """'k=v' lines (the app's line, the tuning table, events)."""
    out = {}
    for line in payload.decode('ascii', 'replace').split('\n'):
        if '=' in line:
            k, v = line.split('=', 1)
            out[k] = v
    return out


# ---------------------------------------------------------------- board -> host
def decode_r(p: bytes) -> dict:
    """'r' raw motion: seq, frames, rate, ranges, flags, n, then n x 18 bytes."""
    n = p[9]
    raw = np.empty((n, 6), dtype=np.int16)
    if len(p) < 10 + 18 * n:
        raise ValueError(f"'r' message cut short: {len(p)} bytes for {n} samples")
    b = np.frombuffer(p, dtype=np.uint8, count=18 * n, offset=10).reshape(n, 6, 3).astype(np.int32)
    u = ((b[:, :, 0] & 3) << 14) | ((b[:, :, 1] & 0x7F) << 7) | (b[:, :, 2] & 0x7F)
    raw[:] = np.where(u >= 32768, u - 65536, u).astype(np.int16)
    ranges = p[7]
    return {'seq': p[0], 'frames': get32(p, 1), 'rate_hz': RATES[p[6] & 3], 'acc_range': ranges & 3,
            'gyro_range': (ranges >> 2) & 3, 'flags': p[8], 'raw': raw}


def decode_q(p: bytes, fields: list[tuple[str, float]]) -> dict:
    """'q' state: seq, frames, then a 14-bit value per announced field (value / scale)."""
    out = {'seq': p[0], 'frames': get32(p, 1)}
    k = 6
    for name, scale in fields:
        if k + 2 > len(p):
            break
        out[name] = get14(p, k) / scale
        k += 2
    return out


def decode_Q(payload: bytes) -> dict:
    kv = text_kv(payload)
    fields = []
    for item in kv.get('fields', '').split(','):
        if ':' in item:
            n, s = item.split(':')
            fields.append((n, float(s)))
    return {'fields': fields, 'states': kv.get('states', 'idle,rec,play').split(','),
            'modes': kv.get('modes', 'TAPE,GRAIN').split(','), 'zones': kv.get('zones', 'dead,fwd,back').split(',')}


def decode_o(p: bytes) -> tuple[int, int, bytes]:
    page, drops = p[0], p[1]
    data = unpack7(p[2:])
    if len(data) != 128:
        raise ValueError(f"'o' page {page}: {len(data)} bytes")
    return page, drops, data


def tune_item(kv: dict[str, str]) -> dict:
    """A 't' description line as PROTOCOL.md's tune item."""
    def f(k, d=0.0):
        try:
            return float(kv.get(k, d))
        except ValueError:
            return d
    name = kv.get('n', '')
    labels = [x for x in kv.get('l', '').split('|') if x] if kv.get('l') else []
    item = {'i': int(f('i')), 'name': name, 'group': name.split('.')[0] if '.' in name else 'other',
            'kind': kv.get('k', 'float'), 'min': f('lo'), 'max': f('hi'), 'step': f('st'), 'def': f('df'),
            'value': f('v'), 'unit': kv.get('u', '').replace('deg', '°'), 'labels': labels, 'help': kv.get('h', '')}
    return item


# ---------------------------------------------------------------- host -> board
def sysex(cmd: str, body: bytes | str = b'') -> bytes:
    if isinstance(body, str):
        body = body.encode('ascii', 'replace')
    if any(b & 0x80 for b in body):
        raise ValueError('SysEx data bytes are 7 bits')
    return b'\xF0\x7D' + cmd.encode('ascii') + body + b'\xF7'


def go(motion=False, state=False, screen=False, events=False) -> bytes:
    return sysex('G', f"m{int(motion)} q{int(state)} o{int(screen)} e{int(events)}")


def keepalive() -> bytes:
    return sysex('G')


def tune_list() -> bytes: return sysex('T', 'l')
def tune_get(i: int) -> bytes: return sysex('T', f'g{i}')
def tune_set(i: int, v: float) -> bytes: return sysex('T', f's{i}={v:.6g}')
def tune_save() -> bytes: return sysex('T', 'w')
def tune_defaults() -> bytes: return sysex('T', 'd')
def tune_preset(p: int) -> bytes: return sysex('T', f'p{p}')


def inject(raw: np.ndarray) -> list[bytes]:
    """Raw chip samples (n, 6) as 'J' messages of up to six samples each (the board's buffer is 128 bytes)."""
    raw = np.asarray(raw, dtype=np.int64)
    out = []
    for i in range(0, len(raw), 6):
        chunk = raw[i:i + 6]
        body = bytearray([len(chunk)])
        for row in chunk:
            for v in row:
                body += put16(int(v))
        out.append(sysex('J', bytes(body)))
    return out


def inject_end() -> bytes:
    """'J' with no samples: the board takes its own sensor again at once (not 0.5 s later)."""
    return sysex('J', bytes([0]))


def key_enc(detents: int) -> bytes: return sysex('K', f'e{int(detents):+d}')
def key_switch(down: bool) -> bytes: return sysex('K', 'E1' if down else 'E0')
def key_rec(down: bool) -> bytes: return sysex('K', 'R1' if down else 'R0')


def key_joy(x: int | None, y: int | None) -> bytes:
    if x is None or y is None:
        return sysex('K', 'x- y-')
    return sysex('K', f'x{int(x)} y{int(y)}')


def info() -> bytes: return sysex('I')
def describe() -> bytes: return sysex('D')
def step(p: int, d: int) -> bytes: return sysex('P', f'p{p:02d}={int(d):+d}' if d else f'p{p:02d}=1')
def status() -> bytes: return sysex('S')
