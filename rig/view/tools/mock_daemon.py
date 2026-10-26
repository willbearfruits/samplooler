#!/usr/bin/env python3
"""A stand-in for slrigd, for working on slrig-view without a board or the real daemon.

It speaks rig/PROTOCOL.md section 2 (a unix socket, JSON lines both ways) with made-up data:

- a board slowly tilting and rolling by hand, with tremor, a gyro bias and sensor noise; the quaternion,
  the accelerometer and the gyro agree with each other (the board's own fusion lags the host's a little and
  its yaw wanders, so the viewer's ghost shows);
- the tuning table read from firmware/main/tune.h (a built-in copy when the file is not there), the
  Samplooler's settings, the gestures from gesture.c, events every couple of seconds;
- the tilt -> speed curve computed as gesture.c's gest_speed() does, with the current tuning;
- the screen: the uisim captures in media/screens/ (decoded from their PNGs) in turn, or moving bars;
- sessions, guided tests with prompts, and now and then a command "by mcp", so the log strip has something.

    python3 rig/view/tools/mock_daemon.py [--socket PATH]
    python3 rig/view/tools/mock_daemon.py --send '{"op": "run", "cmd": "test.run", "args": {"name": "steps"}}'

Python 3 standard library only. The real daemon (rig/slrig) replaces it.
"""
import argparse
import asyncio
import base64
import json
import math
import os
import random
import re
import socket
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))      # the repo (worktree) root
TUNE_H = os.path.join(ROOT, 'firmware', 'main', 'tune.h')
SCREENS = os.path.join(ROOT, 'media', 'screens')

MOTION_HZ = 500
TICK = 0.02                      # 50 Hz: state and motion pushes
D2R, R2D = math.pi / 180, 180 / math.pi


def default_socket():
    run = os.environ.get('XDG_RUNTIME_DIR') or f'/tmp/slrig-mock-{os.getuid()}'
    return os.path.join(run, 'slrig-mock', 'daemon.sock')

# ------------------------------------------------------------------------------------------- quaternions


def qmul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz, aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw)


def qconj(q):
    return (q[0], -q[1], -q[2], -q[3])


def qrot(q, v):
    return qmul(qmul(q, (0.0, *v)), qconj(q))[1:]


def qaxis(axis, ang):
    s = math.sin(ang / 2)
    return (math.cos(ang / 2), axis[0] * s, axis[1] * s, axis[2] * s)


def q_from_angles(pitch, roll, yaw):
    """Device axes into the world: yaw about world z, then pitch (nose down +) about x, roll (right edge down +)
    about y. Rx(+) takes the nose (-y) down and Ry(+) the right edge (+x) down, as PROTOCOL.md's frames say."""
    return qmul(qmul(qaxis((0, 0, 1), yaw * D2R), qaxis((1, 0, 0), pitch * D2R)), qaxis((0, 1, 0), roll * D2R))


def qyaw(q):
    w, x, y, z = q
    return math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * R2D

# ------------------------------------------------------------------------------------------- tuning table


FALLBACK_TUNE = [  # name, kind, min, max, step, def, unit, labels, help (used when tune.h is not found)
    ('imu.acc_range', 'enum', 0, 3, 1, 0, '', '2 g|4 g|8 g|16 g', 'accelerometer full scale'),
    ('imu.gyro_range', 'enum', 0, 3, 1, 2, 'deg/s', '250|500|1000|2000', 'gyro full scale'),
    ('motion.fusion', 'enum', 0, 2, 1, 0, '', 'ACC|COMP|MAHONY', 'how the board finds its tilt'),
    ('tilt.dead', 'float', 0, 30, 0.5, 5, 'deg', '', 'no speed change inside this angle'),
    ('tilt.full', 'float', 10, 90, 1, 45, 'deg', '', 'Speed max at this angle'),
    ('roll.dead', 'float', 0, 45, 0.5, 15, 'deg', '', 'no effect inside this angle'),
    ('roll.full', 'float', 10, 90, 1, 45, 'deg', '', 'full effect at this angle'),
]


def load_tune():
    items = []
    try:
        text = open(TUNE_H).read()
        pat = re.compile(r'X\(\s*(\w+),\s*"([^"]+)",\s*TK_(\w+),\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,'
                         r'\s*([-\d.]+)f?,\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\)')
        for m in pat.finditer(text):
            _, name, kind, lo, hi, st, df, unit, labels, hlp = m.groups()
            items.append((name, kind.lower(), float(lo), float(hi), float(st), float(df), unit, labels, hlp))
    except OSError:
        pass
    if not items:
        items = FALLBACK_TUNE
    out = []
    for i, (name, kind, lo, hi, st, df, unit, labels, hlp) in enumerate(items):
        out.append({'i': i, 'name': name, 'group': name.split('.')[0], 'kind': kind, 'min': lo, 'max': hi,
                    'step': st, 'def': df, 'value': df, 'unit': unit,
                    'labels': labels.split('|') if labels else [], 'help': hlp})
    return out


PRESETS = [('v1', {}), ('gyro', {'motion.fusion': 1, 'motion.rate': 1, 'motion.comp_tau': 0.6, 'motion.acc_gate': 0.2}),
           ('gyro detents', {'motion.fusion': 1, 'motion.rate': 1, 'motion.comp_tau': 0.6, 'motion.acc_gate': 0.2,
                             'tilt.detents': 2, 'tilt.detent_w': 0.12}),
           ('mahony', {'motion.fusion': 2, 'motion.rate': 1, 'motion.kp': 2, 'motion.ki': 0.05, 'motion.acc_gate': 0.2})]

# ------------------------------------------------------------------------------------------- settings

SETTINGS = [  # name, tag, group, kind, min, max, step, value, labels or unit, help
    ('Mic gain', 'mic ', 'SOUND', 'int', 0, 36, 1, 18, 'dB', 'How hard the mic listens.'),
    ('Mode', 'mode', 'PLAY', 'enum', 0, 1, 1, 0, ['TAPE', 'GRAIN'], 'TAPE: the pitch follows the speed. GRAIN: they are apart.'),
    ('Pitch', 'ptch', 'PLAY', 'int', -12, 12, 1, 0, 'st', 'Semitones, in GRAIN only.'),
    ('Tilt', 'tilt', 'TILT', 'enum', 0, 1, 1, 1, ['OFF', 'ON'], 'Off: it plays forward at 1x, whatever the angle.'),
    ('Speed max', 'smax', 'TILT', 'float', 0.25, 8, 0.25, 2.0, 'x', 'The speed at 45 degrees.'),
    ('Speed min', 'smin', 'TILT', 'float', 0, 1, 0.05, 0.05, 'x', 'The crawl when flat. 0 stops the tape.'),
    ('Snap', 'snap', 'PLAY', 'enum', 0, 1, 1, 0, ['OFF', 'ON'], 'Speeds rounded to octaves and fifths.'),
    ('Grain', 'grn ', 'PLAY', 'int', 10, 200, 10, 50, 'ms', 'The grain\'s length, in GRAIN only.'),
    ('Volume', 'vol ', 'SOUND', 'int', 0, 150, 5, 100, '%', 'Above 100% it can clip.'),
    ('Input', 'inpt', 'SOUND', 'enum', 0, 1, 1, 0, ['MIC', 'USB'], 'USB records what the computer plays into the board.'),
    ('Voice', 'voic', 'VOICE', 'enum', 0, 6, 1, 0, ['OFF', 'ROBOT', 'CHOIR', 'OCTAVE', 'VOCODER', 'WHISPER', 'MONK'],
     'A voice on the input, before the recorder.'),
    ('Key', 'key ', 'VOICE', 'enum', 0, 11, 1, 9, ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'],
     'The key the voice tunes to.'),
    ('Scale', 'scal', 'VOICE', 'enum', 0, 3, 1, 1, ['MAJOR', 'MINOR', 'PENTA', 'CHROM'], 'The notes of the key.'),
    ('Monitor', 'mont', 'SOUND', 'enum', 0, 2, 1, 1, ['OFF', 'REC', 'ALWAYS'], 'When the input (the voice) is heard.'),
    ('Slot', 'slot', 'SLOTS', 'int', 1, 8, 1, 1, '', 'The slot Save and Load use, with its length.'),
    ('Save', 'save', 'SLOTS', 'action', 0, 0, 1, 0, '', 'Writes the loop to the slot.'),
    ('Load', 'load', 'SLOTS', 'action', 0, 0, 1, 0, '', 'Reads the slot back into the loop.'),
    ('Speaker', 'spkr', 'SOUND', 'enum', 0, 1, 1, 1, ['OFF', 'ON'], 'Off: the line out and USB only.'),
    ('Rec button', 'rbtn', 'PLAY', 'enum', 0, 1, 1, 1, ['OFF', 'ON'], 'Off: the stick\'s button is ignored.'),
    ('Link', 'nlnk', 'LINK', 'enum', 0, 1, 1, 1, ['OFF', 'ON'], 'The radio to the other instruments.'),
    ('Group', 'ngrp', 'LINK', 'int', 1, 8, 1, 1, '', 'Boards in the same group play together.'),
    ('Range', 'npow', 'LINK', 'enum', 0, 2, 1, 1, ['NEAR', 'ROOM', 'FAR'], 'The radio\'s power.'),
    ('Who is there', 'who ', 'LINK', 'action', 0, 0, 1, 0, '', 'Names the boards heard right now, on the board\'s screen.'),
]


def make_settings():
    """The board's settings as the mock keeps them (a value each); settings_public() is what goes on the wire."""
    out = []
    for i, (name, tag, group, kind, lo, hi, st, v, lab, hlp) in enumerate(SETTINGS):
        labels = lab if isinstance(lab, list) else []
        out.append({'i': i, 'tag': f'p{i:02d}', 'name': name, 'group': group, 'kind': kind, 'min': lo, 'max': hi,
                    'step': st, 'value': v, 'unit': '' if labels else lab, 'text': '', 'labels': labels, 'help': hlp})
    for s in out:
        settings_text(s)
    return out


def settings_public(items):
    """PROTOCOL.md's setting item: stepped, never set; "act" items run with delta 1 and may ask first."""
    order = ['PLAY', 'TILT', 'SOUND', 'VOICE', 'SLOTS', 'LINK', 'SETTINGS']
    pub = []
    for s in sorted(items, key=lambda s: (order.index(s['group']) if s['group'] in order else 99, s['i'])):
        p = {'i': s['i'], 'tag': s['tag'], 'name': s['name'], 'group': s['group'],
             'kind': 'act' if s['kind'] == 'action' else 'step', 'text': s['text'], 'help': s['help']}
        if s['name'] == 'Load':
            p['confirm'] = 'Load the slot? The loop that plays now is replaced.'
        elif s['name'] == 'Save':
            p['confirm'] = 'Save the loop to the slot? What the slot held is replaced.'
        pub.append(p)
    return pub


def settings_text(s):
    if s['labels']:
        s['text'] = s['labels'][int(s['value']) - int(s['min'])]
    elif s['kind'] == 'action':
        s['text'] = ''
    elif s['kind'] == 'float':
        s['text'] = f"{s['value']:.2f}{s['unit']}"
    else:
        s['text'] = f"{int(s['value'])} {s['unit']}".strip()


# ------------------------------------------------------------------------------------------- gestures


GESTURES = [
    ('tilt.zone', 'tilt', 'the tilt leaves or enters the dead zone (|angle| vs tilt.dead + tilt.hyst)',
     ['tilt.dead', 'tilt.hyst', 'tilt.full', 'tilt.sign']),
    ('tilt.detent', 'tilt', 'the speed lands within tilt.detent_w of a detent', ['tilt.detents', 'tilt.detent_w']),
    ('tilt.freeze', 'tilt', 'the knob held in play, or note 64: the angles stay', ['enc.hold']),
    ('roll.delay', 'roll', 'rolled left past roll.dead', ['roll.dead', 'roll.full', 'roll.sign']),
    ('roll.stutter', 'roll', 'rolled right past roll.dead', ['roll.dead', 'roll.full', 'roll.sign']),
    ('roll.off', 'roll', 'back inside roll.dead', ['roll.dead']),
    ('enc.turn', 'enc', 'detents counted (enc.steps counts each, enc.glitch filters bounce)',
     ['enc.steps', 'enc.dir', 'enc.glitch', 'enc.accel']),
    ('enc.down', 'enc', 'the knob pressed', []),
    ('enc.up', 'enc', 'the knob let go (ms = how long it was down)', []),
    ('enc.tap', 'enc', 'let go after enc.tap_min .. enc.hold', ['enc.tap_min', 'enc.hold']),
    ('enc.hold', 'enc', 'let go after enc.hold or more', ['enc.hold']),
    ('enc.long', 'enc', 'still down after enc.long', ['enc.long']),
    ('enc.bounce', 'enc', 'let go before enc.tap_min', ['enc.tap_min']),
    ('rec.down', 'rec', 'REC pressed (rec.debounce ticks agree)', ['rec.debounce']),
    ('rec.up', 'rec', 'REC let go (ms = how long it was down)', ['rec.debounce']),
    ('rec.reset', 'rec', 'REC held rec.reset_ms from power-on, then let go', ['rec.reset_ms']),
    ('rec.stuck', 'rec', 'REC down rec.stuck_ms from power-on', ['rec.stuck_ms']),
    ('joy.flick', 'joy', 'past joy.flick, re-armed back inside joy.rearm', ['joy.flick', 'joy.rearm', 'joy.dead']),
    ('menu.open', 'enc', 'a tap in play', ['enc.tap_min', 'enc.hold']),
    ('menu.close', 'enc', 'a hold in the menu', ['enc.hold']),
    ('rec.start', 'rec', 'recording starts', []),
    ('rec.stop', 'rec', 'recording stops: the loop plays', []),
]

TESTS = [
    ('still', 'Still', 'Put the board on the table and leave it.',
     [('Put the board flat on the table', 3), ('Hands off: keep it still', 10)]),
    ('steps', 'Tilt steps', 'Tilt and hold at 0, 15, 30 and 45 degrees each way.',
     [('Hold it flat', 4), ('Nose down 15 degrees and hold', 4), ('Nose down 30 degrees and hold', 4),
      ('Nose down 45 degrees and hold', 4), ('Back to flat', 4), ('Nose up 15 degrees and hold', 4),
      ('Nose up 30 degrees and hold', 4), ('Nose up 45 degrees and hold', 4)]),
    ('sweep', 'Slow sweep', 'One slow sweep from nose up to nose down and back.',
     [('From flat, tilt slowly nose down to 60 degrees', 6), ('Now slowly nose up to 60 degrees', 8),
      ('Back to flat', 4)]),
    ('flicks', 'Flicks', 'Short sharp flicks of the wrist.', [('Flick the nose down five times', 8),
                                                              ('Flick the nose up five times', 8)]),
    ('shake', 'Shake', 'Shake it hard for a few seconds.', [('Shake the board', 5), ('Hold it still', 4)]),
    ('free', 'Free play', 'Play it as you would.', [('Play', 30)]),
]

# ------------------------------------------------------------------------------------------- the screen


def png_rgb(path):
    """Decode an 8-bit RGB or RGBA PNG (no interlace) with zlib alone: (w, h, rows of bytes, channels)."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            if depth != 8 or ctype not in (2, 6) or inter:
                raise ValueError('unsupported PNG')
            ch = 3 if ctype == 2 else 4
        elif typ == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * ch
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, rows, ch


def fb_from_capture(path):
    """uisim's 408 x 216 captures: a 12 px frame, 3 x 3 screen pixels. Back to the 1024-byte page format."""
    w, h, rows, ch = png_rgb(path)
    cell = (w - 24) // 128
    fb = bytearray(1024)
    for y in range(64):
        for x in range(128):
            px = rows[12 + y * cell + cell // 2]
            o = (12 + x * cell + cell // 2) * ch
            if max(px[o], px[o + 1], px[o + 2]) > 110:
                fb[(y >> 3) * 128 + x] |= 1 << (y & 7)
    return bytes(fb)


def fb_bars(t):
    """Made-up screen: a header block, moving bars and text-like blocks."""
    fb = bytearray(1024)

    def px(x, y):
        if 0 <= x < 128 and 0 <= y < 64:
            fb[(y >> 3) * 128 + x] |= 1 << (y & 7)
    for x in range(2, 60):
        for y in range(3, 12):
            if (x // 4 + y // 3) % 2 == 0 or y in (3, 11):
                px(x, y)
    for i in range(16):
        hgt = int(18 + 16 * math.sin(t * 2.0 + i * 0.6))
        for x in range(4 + i * 7, 9 + i * 7):
            for y in range(63 - hgt, 62):
                px(x, y)
    return bytes(fb)

# ------------------------------------------------------------------------------------------- the mock


class Mock:
    def __init__(self):
        self.t0 = time.monotonic()
        self.clients = set()
        self.tune = load_tune()
        self.tname = {it['name']: it for it in self.tune}
        self.settings = make_settings()
        self.ab = {'A': None, 'B': None}
        self.gest = {g[0]: {'name': g[0], 'group': g[1], 'rule': g[2], 'tune': [n for n in g[3] if n in self.tname],
                            'count': 0, 'last_t': None} for g in GESTURES}
        self.sessions = [
            {'name': '2026-10-26-still', 'date': '2026-10-26 22:41', 'secs': 13.0, 'test': 'still', 'samples': 6500},
            {'name': '2026-10-26-steps', 'date': '2026-10-26 22:44', 'secs': 32.0, 'test': 'steps', 'samples': 16000},
            {'name': '2026-10-26-play', 'date': '2026-10-26 22:52', 'secs': 41.5, 'test': '', 'samples': 20750},
        ]
        self.recording = None           # (name, start t)
        self.test_task = None
        self.prompt = None
        self.cmd_log = []
        self.log_lines = []
        self.seq = 0
        self.drops = {'motion': 0, 'state': 0, 'screen': 0}
        self.stream = {'motion': True, 'state': True, 'screen': True, 'events': True}
        self.screens = []
        if os.path.isdir(SCREENS):
            for f in ('ready', 'recording', 'reverse', 'delay', 'stutter', 'grain', 'flung', 'menu-save'):
                p = os.path.join(SCREENS, f + '.png')
                try:
                    self.screens.append(fb_from_capture(p))
                except (OSError, ValueError, IndexError, zlib.error):
                    pass
        self.fb = b'\0' * 1024
        self.fb_i = -1
        # motion state
        self.acc_f = [0.0, 0.0, 1.0]
        self.speed_now = 0.05
        self.pos = 0.0
        self.enc = 0
        self.zone = 'dead'
        self.fx_on = {'delay': False, 'stutter': False}
        self.injected_until = 0.0
        self.bias = (0.82, -0.47, 0.31)
        self.last_event = 0.0
        self.next_event = 2.0
        self.next_mcp = 4.0
        self.mcp_step = 0

    def now(self):
        return time.monotonic() - self.t0

    def tv(self, name, default=0.0):
        it = self.tname.get(name)
        return it['value'] if it else default

    def setting(self, name):
        for s in self.settings:
            if s['name'] == name:
                return s['value']
        return 0

    # ------------------------------------------------------------------ the hand
    def angles(self, t):
        """The true orientation: a slow hand, tremor, now and then a flick."""
        pitch = 24 * math.sin(2 * math.pi * t / 9.0) + 9 * math.sin(2 * math.pi * t / 3.7 + 0.4)
        roll = 21 * math.sin(2 * math.pi * t / 13.0 + 1.0) + 4 * math.sin(2 * math.pi * t / 2.9)
        yaw = 28 * math.sin(2 * math.pi * t / 21.0) + 5 * math.sin(2 * math.pi * t / 5.3)
        trem = 0.35 * math.sin(2 * math.pi * 8.3 * t) + 0.2 * math.sin(2 * math.pi * 10.9 * t + 1.3)
        pitch += trem
        roll += 0.8 * trem
        ph = t % 17.0
        if 11.0 < ph < 11.35:                          # a flick, nose down and back
            pitch += 30 * math.sin(math.pi * (ph - 11.0) / 0.35)
        return pitch, roll, yaw

    def q_true(self, t):
        return q_from_angles(*self.angles(t))

    def sample(self, t):
        q = self.q_true(t)
        acc = qrot(qconj(q), (0.0, 0.0, 1.0))
        h = 1e-3
        dq = qmul(qconj(q), self.q_true(t + h))
        gyro = [2 * dq[k + 1] / h * R2D for k in range(3)]
        acc = [a + random.gauss(0, 0.004) for a in acc]
        gyro = [g + b + random.gauss(0, 0.08) for g, b in zip(gyro, self.bias)]
        return acc, gyro

    # ------------------------------------------------------------------ the curve (gesture.c's gest_speed)
    def speed_of(self, u, live=None):
        full = self.tv('tilt.full', 45)
        dead = self.tv('tilt.dead', 5) / full
        smin, smax = self.setting('Speed min'), self.setting('Speed max')
        mag = abs(u)
        if live is None:
            live = mag >= dead
        if not live:
            return smin, 0.0
        tt = min(1.0, (mag - dead) / (1.0 - dead)) if dead < 1 else 1.0
        c = self.tv('tilt.curve', 1)
        soft = tt * tt * (1.6 - 0.6 * tt)
        curve = soft if c >= 1 else tt if c <= 0 else (1 - c) * tt + c * soft
        r = smin + (smax - smin) * curve
        held = 0.0
        sets = {1: [1], 2: [1, 2], 3: [0.25, 0.5, 1, 2, 4, 8]}.get(int(self.tv('tilt.detents', 0)))
        if sets and r > 0:
            bx, best = min(((math.log2(r / s), s) for s in sets), key=lambda p: abs(p[0]))
            w, s, a = self.tv('tilt.detent_w', 0.12), 0.5, abs(bx)
            if a < w:
                r, held = best, best
            elif a < w + s:
                r = best * 2 ** ((-1 if bx < 0 else 1) * (a - w) * ((w + s) / s))
        return (r if u >= 0 else -r), held

    def curve(self, n=121):
        n = max(5, min(2001, int(n)))
        sign = 1.0 if int(self.tv('tilt.sign', 1)) else -1.0
        full = self.tv('tilt.full', 45)
        lim = max(60.0, full * 1.33)
        angle, speed, det = [], [], {}
        for k in range(n):
            a = -lim + 2 * lim * k / (n - 1)
            s, held = self.speed_of(a * sign / full)
            angle.append(round(a, 3))
            speed.append(round(s, 4))
            if held:
                key = (held, a > 0)
                lo, hi = det.get(key, (a, a))
                det[key] = (min(lo, a), max(hi, a))
        rd, rf = self.tv('roll.dead', 15), self.tv('roll.full', 45)
        rsign = 1.0 if int(self.tv('roll.sign', 1)) else -1.0
        on = int(self.tv('roll.on', 1))
        ra, dl, stt = [], [], []
        for k in range(n):
            a = -lim + 2 * lim * k / (n - 1)
            r = a * rsign if on else 0
            x = 0.0 if abs(r) < rd else min(1.0, (abs(r) - rd) / max(1e-6, rf - rd))
            ra.append(round(a, 3))
            dl.append(round(x if r < 0 else 0.0, 4))
            stt.append(round(x if r > 0 else 0.0, 4))
        return {'tilt': {'angle': angle, 'speed': speed, 'dead': self.tv('tilt.dead', 5), 'full': full,
                         'hyst': self.tv('tilt.hyst', 0), 'sign': sign,
                         'speed_min': self.setting('Speed min'), 'speed_max': self.setting('Speed max'),
                         'detents': [{'speed': (k[0] if k[1] else -k[0]) * sign, 'lo': v[0], 'hi': v[1]}
                                     for k, v in sorted(det.items())]},
                'roll': {'angle': ra, 'delay': dl, 'stutter': stt, 'dead': rd, 'full': rf, 'sign': rsign}}

    # ------------------------------------------------------------------ pushes
    async def push(self, ev, msg, stream=None):
        msg = {'ev': ev, **msg}
        line = (json.dumps(msg, separators=(',', ':')) + '\n').encode()
        for c in list(self.clients):
            if (stream or ev) in c.subs:
                c.send(line, droppable=ev in ('motion', 'state'))

    def status(self):
        return {'source': 'twin' if os.environ.get('SLRIG_MOCK_TWIN') else 'board',
                'board': {'name': 'Samplooler', 'serial': 'SL-727600D0', 'version': '0.1.0-rig-mock', 'rig': 1},
                'rates': {'motion_hz': MOTION_HZ if self.stream['motion'] else 0,
                          'state_hz': 50 if self.stream['state'] else 0,
                          'screen_fps': round(1 / 1.6, 2) if self.stream['screen'] else 0},
                'drops': dict(self.drops), 'recording': self.recording is not None,
                'osc': {'out': '127.0.0.1:9001', 'in': 9000}, 'mock': True}

    async def log(self, text, level='info'):
        t = round(self.now(), 3)
        self.log_lines.append({'t': t, 'text': text, 'level': level})
        self.log_lines = self.log_lines[-500:]
        await self.push('log', {'t': t, 'text': text, 'level': level})

    async def event(self, name, v=0.0, ms=0, text=''):
        t = round(self.now(), 3)
        g = self.gest.get(name)
        if g:
            g['count'] += 1
            g['last_t'] = t
        await self.push('event', {'t': t, 'name': name, 'v': v, 'ms': ms, 'text': text}, 'events')

    # ------------------------------------------------------------------ the board's loop
    async def run_board(self):
        next_t = time.monotonic()
        last_status = -1.0
        tick = 0
        while True:
            next_t += TICK
            await asyncio.sleep(max(0.0, next_t - time.monotonic()))
            t = self.now()
            tick += 1
            n = int(MOTION_HZ * TICK)
            ts = [t - TICK + (k + 1) / MOTION_HZ for k in range(n)]
            samples = [self.sample(x) for x in ts]
            q_ref = self.q_true(t)
            # the board's fusion: a little behind, and its yaw wanders (no compass)
            p, r, y = self.angles(t - 0.05)
            q_board = q_from_angles(p, r, y + 7 * math.sin(2 * math.pi * t / 30.0))
            if self.stream['motion']:
                await self.push('motion', {'t0': round(ts[0], 4), 'dt': 1 / MOTION_HZ,
                                           'acc': [[round(v, 4) for v in s[0]] for s in samples],
                                           'gyro': [[round(v, 3) for v in s[1]] for s in samples],
                                           'raw': False, 'ref_quat': [round(v, 5) for v in q_ref],
                                           'injected': t < self.injected_until})
            # ACC (v1): the one-pole on the accelerometer, then two atan2
            k = self.tv('motion.acc_smooth', 0.25)
            a = samples[-1][0]
            self.acc_f = [f + k * (x - f) for f, x in zip(self.acc_f, a)]
            ax, ay, az = self.acc_f
            pitch = math.atan2(ay, math.hypot(ax, az)) * R2D
            roll = math.atan2(-ax, math.hypot(ay, az)) * R2D
            sign = 1.0 if int(self.tv('tilt.sign', 1)) else -1.0
            full = self.tv('tilt.full', 45)
            u = (roll if int(self.tv('tilt.axis', 0)) else pitch) * sign / full
            tilt_on = int(self.setting('Tilt'))
            speed_cmd, held = self.speed_of(u) if tilt_on else (1.0, 0.0)
            live = abs(u) >= self.tv('tilt.dead', 5) / full
            zone = 'detent' if held else ('dead' if not live else 'fwd' if u >= 0 else 'back')
            if zone != self.zone and self.stream['events']:
                await self.event('tilt.zone', {'dead': 0, 'fwd': 1, 'back': 2, 'detent': 3}[zone], 0, zone)
            self.zone = zone
            slew = max(0.001, self.tv('tilt.slew', 50) / 1000)
            self.speed_now += (speed_cmd - self.speed_now) * min(1.0, TICK / slew)
            loop_s = 4.2
            self.pos = (self.pos + self.speed_now * TICK / loop_s) % 1.0
            rd, rf = self.tv('roll.dead', 15), self.tv('roll.full', 45)
            rr = roll * (1 if int(self.tv('roll.sign', 1)) else -1)
            amt = 0.0 if abs(rr) < rd else min(1.0, (abs(rr) - rd) / max(1e-6, rf - rd))
            delay, stutter = (amt if rr < 0 else 0.0), (amt if rr > 0 else 0.0)
            for name, v in (('delay', delay), ('stutter', stutter)):
                if (v > 0) != self.fx_on[name]:
                    self.fx_on[name] = v > 0
                    await self.event('roll.' + name if v > 0 else 'roll.off', round(v, 3), 0, name)
            in_peak = min(1.0, abs(0.18 + 0.12 * math.sin(t * 1.7) + 0.1 * math.sin(t * 7.1) + random.gauss(0, 0.03)))
            out_peak = min(1.0, abs(0.32 + 0.2 * math.sin(t * 2.3 + self.pos * 6.28) + random.gauss(0, 0.04)))
            gyro = samples[-1][1]
            rest = max(abs(g - b) for g, b in zip(gyro, self.bias)) < 3
            self.seq += 1
            if self.stream['state']:
                await self.push('state', {
                    't': round(t, 4), 'seq': self.seq, 'state': 'play', 'mode': 'TAPE',
                    'quat': [round(v, 5) for v in q_board],
                    'pitch': round(pitch, 3), 'roll': round(roll, 3), 'yaw': round(qyaw(q_board), 3),
                    'tilt_u': round(max(-1.5, min(1.5, u)), 4), 'zone': zone,
                    'speed_cmd': round(speed_cmd, 4), 'speed_now': round(self.speed_now, 4),
                    'pos': round(self.pos, 4), 'loop_s': loop_s, 'delay': round(delay, 4), 'stutter': round(stutter, 4),
                    'joy_x': 0.0, 'joy_y': 0.0, 'joy_rx': 2048 + int(random.gauss(0, 3)),
                    'joy_ry': 1990 + int(random.gauss(0, 3)), 'enc': self.enc, 'enc_down': False, 'rec_down': False,
                    'frozen': False, 'menu': False, 'injected': t < self.injected_until, 'joy_present': True,
                    'imu_present': True, 'recording': self.recording is not None, 'playing': True,
                    'in_peak': round(in_peak, 4), 'out_peak': round(out_peak, 4), 'in_clips': 0,
                    'cpu': round(31 + 3 * math.sin(t / 3) + random.random(), 1), 'heap_kb': 58, 'i2c_fail': 0,
                    'bias': list(self.bias), 'rest': rest})
            # the screen: the next capture every 1.6 s
            i = int(t / 1.6)
            if self.stream['screen'] and i != self.fb_i:
                self.fb_i = i
                self.fb = self.screens[i % len(self.screens)] if self.screens else fb_bars(t)
                await self.push('screen', self.screen_msg())
            elif not self.screens and self.stream['screen'] and tick % 5 == 0:
                self.fb = fb_bars(t)
                await self.push('screen', self.screen_msg())
            if self.stream['events'] and t >= self.next_event:
                await self.random_gesture(t)
                self.next_event = t + random.uniform(1.2, 2.8)
            if t >= self.next_mcp:
                await self.fake_mcp()
                self.next_mcp = t + random.uniform(5, 9)
            if t - last_status >= 1.0:
                last_status = t
                await self.push('status', self.status())
                if self.recording:
                    await self.push('session', {'state': 'recording', 'name': self.recording[0],
                                                'secs': round(t - self.recording[1], 1)})

    def screen_msg(self):
        return {'w': 128, 'h': 64, 'fb': base64.b64encode(self.fb).decode()}

    async def random_gesture(self, t):
        pick = random.choice(['enc.turn', 'enc.turn', 'enc.tap', 'enc.hold', 'rec', 'joy.flick', 'enc.bounce'])
        if pick == 'enc.turn':
            d = random.choice([-2, -1, 1, 1, 2, 3])
            self.enc += d
            await self.event('enc.turn', d, 0, '')
        elif pick in ('enc.tap', 'enc.hold', 'enc.bounce'):
            ms = {'enc.tap': random.randint(90, 320), 'enc.hold': random.randint(560, 1400),
                  'enc.bounce': random.randint(8, 30)}[pick]
            await self.event('enc.down', 0, 0, '')
            await asyncio.sleep(0)
            await self.event('enc.up', 0, ms, '')
            await self.event(pick, 0, ms, '')
            if pick == 'enc.tap':
                await self.event('menu.open', 0, 0, '')
            elif pick == 'enc.hold':
                await self.event('menu.close', 0, 0, '')
        elif pick == 'rec':
            ms = random.randint(700, 2600)
            await self.event('rec.down', 0, 0, '')
            await self.event('rec.start', 0, 0, '')
            await self.event('rec.up', 0, ms, '')
            await self.event('rec.stop', round(ms / 1000, 2), ms, f'{ms / 1000:.2f} s')
            await self.log(f'I ({int(t * 1000)}) main: rec {ms / 1000:.2f} s')
        else:
            v = random.choice([-0.82, 0.77, 0.91, -0.74])
            await self.event('joy.flick', v, 0, random.choice(['left', 'right', 'up', 'down']))

    async def fake_mcp(self):
        """Now and then 'Claude' runs something, as the MCP would."""
        steps = [('tune.get', {'name': 'tilt.dead'}), ('curve', {'n': 61}), ('session.list', {}),
                 ('session.analyze', {'name': '2026-10-26-still'}), ('state', {}), ('tune.get', {'name': 'motion.fusion'})]
        cmd, args = steps[self.mcp_step % len(steps)]
        self.mcp_step += 1
        await self.cmds_push('mcp', cmd, args, True, random.randint(2, 40))

    async def cmds_push(self, by, cmd, args, ok, ms):
        entry = {'t': round(self.now(), 3), 'by': by, 'cmd': cmd, 'args': args, 'ok': ok, 'ms': ms}
        self.cmd_log.append(entry)
        self.cmd_log = self.cmd_log[-200:]
        await self.push('cmds', entry)

    # ------------------------------------------------------------------ guided tests
    async def run_test(self, name):
        spec = next((x for x in TESTS if x[0] == name), None)
        steps = spec[3]
        sess = f'{time.strftime("%Y-%m-%d")}-{name}'
        self.recording = (sess, self.now())
        await self.push('session', {'state': 'recording', 'name': sess, 'secs': 0})
        try:
            for i, (text, secs) in enumerate(steps):
                for phase, dur in (('ready', 3.0), ('go', float(secs))):
                    end = time.monotonic() + dur
                    while True:
                        left = end - time.monotonic()
                        if left <= 0:
                            break
                        self.prompt = {'test': name, 'step': i + 1, 'of': len(steps),
                                       'text': text if phase == 'go' else f'Get ready: {text.lower()}',
                                       'secs': round(left, 2), 'phase': phase, 'total': dur}
                        await self.push('prompt', self.prompt)
                        await asyncio.sleep(min(0.5, left))
            self.prompt = {'test': name, 'step': len(steps), 'of': len(steps), 'text': 'Done', 'secs': 0,
                           'phase': 'done', 'total': 0}
            await self.push('prompt', self.prompt)
            await asyncio.sleep(1.5)
        finally:
            secs = round(self.now() - self.recording[1], 1)
            self.sessions.append({'name': sess, 'date': time.strftime('%Y-%m-%d %H:%M'), 'secs': secs,
                                  'test': name, 'samples': int(secs * MOTION_HZ)})
            self.recording = None
            self.prompt = None
            await self.push('prompt', {'test': name, 'step': 0, 'of': 0, 'text': '', 'secs': 0, 'phase': 'done'})
            await self.push('session', {'state': 'idle', 'name': sess, 'secs': secs})
            self.test_task = None

    def analyze(self, name):
        s = next((x for x in self.sessions if x['name'] == name), None)
        if s is None:
            raise KeyError(name)
        rnd = random.Random(name)
        b = [v + rnd.gauss(0, 0.02) for v in self.bias]
        rows = [
            ['gyro bias', *[round(v, 3) for v in b], '°/s'],
            ['gyro noise (rms)', *[round(0.08 + rnd.random() * 0.02, 3) for _ in range(3)], '°/s'],
            ['accel noise (rms)', *[round(0.0038 + rnd.random() * 0.0008, 4) for _ in range(3)], 'g'],
            ['accel mean', round(rnd.gauss(0, 0.004), 4), round(rnd.gauss(0, 0.004), 4), round(1 + rnd.gauss(0, 0.003), 4), 'g'],
            ['yaw drift', '', '', round(0.4 + rnd.random() * 0.3, 2), '°/min'],
            ['speed jitter at rest', round(0.002 + rnd.random() * 0.002, 4), '', '', 'x (rms)'],
            ['step lag (63 %)', round(70 + rnd.random() * 30, 0), '', '', 'ms'],
            ['overshoot', round(rnd.random() * 2, 1), '', '', '%'],
            ['saturated samples', 0, 0, 0, ''],
            ['I2C failures', 0, '', '', ''],
            ['motion drops', rnd.randint(0, 3), '', '', 'messages'],
        ]
        return {'name': name, 'secs': s['secs'], 'test': s['test'],
                'summary': 'still enough for a bias: |gyro| < 3 °/s for the whole take' if s['test'] == 'still'
                else 'looks clean: no saturation, no NaN, no I2C failures',
                'table': {'columns': ['', 'x', 'y', 'z', 'unit'], 'rows': rows}}

    # ------------------------------------------------------------------ commands
    async def run_cmd(self, client, cmd, args):
        args = args or {}

        def item(name):
            it = self.tname.get(name)
            if it is None:
                raise CmdError('tune.unknown', f"no tuning value '{name}'", 'tune.list shows the names')
            return it

        def set_value(it, v):
            v = float(v)
            v = min(it['max'], max(it['min'], v))
            if it['kind'] != 'float':
                v = float(int(round(v)))
            it['value'] = v
            return v

        if cmd == 'help':
            return {'commands': sorted(COMMANDS)}
        if cmd == 'status':
            return self.status()
        if cmd == 'state':
            return {'zone': self.zone, 'speed_now': self.speed_now}
        if cmd in ('connect', 'disconnect'):
            return {'source': 'board' if cmd == 'connect' else 'none', 'mock': True}
        if cmd == 'stream':
            for k in ('motion', 'state', 'screen', 'events'):
                if k in args:
                    self.stream[k] = bool(args[k])
            return dict(self.stream)
        if cmd == 'tune.list':
            return {'items': self.tune, 'presets': [p[0] for p in PRESETS]}
        if cmd == 'tune.get':
            return item(args.get('name', ''))
        if cmd == 'tune.set':
            it = item(args.get('name', ''))
            if it['kind'] == 'action':
                await self.log(f"I ({int(self.now() * 1000)}) tune: {it['name']}")
                return {'name': it['name'], 'ran': True}
            v = set_value(it, args.get('value', it['value']))
            await self.push('tune', {'items': self.tune})
            return {'name': it['name'], 'value': v}
        if cmd == 'tune.save':
            await self.log(f'I ({int(self.now() * 1000)}) tune: tuning saved ({len(self.tune)} values)')
            return {'saved': True}
        if cmd == 'tune.reset':
            for it in self.tune:
                it['value'] = it['def']
            await self.push('tune', {'items': self.tune})
            return {'reset': True}
        if cmd == 'tune.preset':
            n = int(args.get('n', 0))
            if not 0 <= n < len(PRESETS):
                raise CmdError('tune.preset', f'no preset {n}', f'0..{len(PRESETS) - 1}')
            for it in self.tune:
                it['value'] = it['def']
            for k, v in PRESETS[n][1].items():
                if k in self.tname:
                    set_value(self.tname[k], v)
            await self.push('tune', {'items': self.tune})
            return {'preset': PRESETS[n][0]}
        if cmd == 'tune.ab':
            slot = str(args.get('slot', 'A')).upper()
            if slot not in self.ab:
                raise CmdError('tune.ab', f'no slot {slot}', 'A or B')
            if args.get('store'):
                self.ab[slot] = {it['name']: it['value'] for it in self.tune}
                return {'slot': slot, 'stored': True}
            if self.ab[slot] is None:
                raise CmdError('tune.ab.empty', f'slot {slot} holds nothing yet', 'store it first')
            for it in self.tune:
                it['value'] = self.ab[slot].get(it['name'], it['value'])
            await self.push('tune', {'items': self.tune})
            return {'slot': slot, 'recalled': True}
        if cmd == 'settings.list':
            return {'items': settings_public(self.settings)}
        if cmd == 'settings.step':
            i, d = int(args.get('index', -1)), int(args.get('delta', 0))
            if not 0 <= i < len(self.settings):
                raise CmdError('settings.index', f'no setting {i}', 'settings.list shows them')
            s = self.settings[i]
            if s['kind'] == 'action':
                await self.log(f"I ({int(self.now() * 1000)}) main: {s['name'].lower()} slot {int(self.setting('Slot'))}")
                return {'index': i, 'ran': True}
            if d not in (-1, 1):
                raise CmdError('settings.delta', 'delta is +1 or -1', '')
            s['value'] = min(s['max'], max(s['min'], round(s['value'] + d * s['step'], 4)))
            settings_text(s)
            await self.push('settings', {'items': settings_public(self.settings)})
            return {'index': i, 'value': s['value'], 'text': s['text']}
        if cmd == 'gestures':
            return list(self.gest.values())
        if cmd == 'curve':
            return self.curve(args.get('n', 121))
        if cmd == 'key':
            if 'enc' in args:
                self.enc += int(args['enc'])
                await self.event('enc.turn', int(args['enc']), 0, 'from the PC')
            return {'ok': True}
        if cmd == 'joy.override':
            return {'x': args.get('x'), 'y': args.get('y')}
        if cmd in ('inject.take', 'inject.gesture'):
            self.injected_until = self.now() + float(args.get('secs', 3))
            return {'injecting': True, 'secs': float(args.get('secs', 3))}
        if cmd == 'inject.stop':
            self.injected_until = 0
            return {'injecting': False}
        if cmd == 'record.start':
            if self.recording:
                raise CmdError('record.busy', 'already recording', 'record.stop first')
            name = f"{time.strftime('%Y-%m-%d')}-{args.get('name') or 'take'}"
            self.recording = (name, self.now())
            await self.push('session', {'state': 'recording', 'name': name, 'secs': 0})
            await self.push('status', self.status())
            return {'name': name}
        if cmd == 'record.stop':
            if not self.recording:
                raise CmdError('record.idle', 'not recording', '')
            name, t0 = self.recording
            secs = round(self.now() - t0, 1)
            self.sessions.append({'name': name, 'date': time.strftime('%Y-%m-%d %H:%M'), 'secs': secs, 'test': '',
                                  'samples': int(secs * MOTION_HZ)})
            self.recording = None
            await self.push('session', {'state': 'idle', 'name': name, 'secs': secs})
            await self.push('status', self.status())
            return {'name': name, 'secs': secs}
        if cmd == 'session.list':
            return {'items': self.sessions}
        if cmd == 'session.info':
            s = next((x for x in self.sessions if x['name'] == args.get('name')), None)
            if s is None:
                raise CmdError('session.unknown', f"no session '{args.get('name')}'", 'session.list')
            return s
        if cmd == 'session.analyze':
            try:
                return self.analyze(args.get('name', ''))
            except KeyError:
                raise CmdError('session.unknown', f"no session '{args.get('name')}'", 'session.list')
        if cmd in ('session.replay', 'session.compare', 'audio.measure', 'screen.png', 'view.shot', 'plot'):
            raise CmdError('mock.missing', f'{cmd} is not in the mock daemon', 'the real slrigd has it')
        if cmd == 'test.list':
            return {'items': [{'name': n, 'title': ti, 'help': h, 'steps': len(st), 'secs': sum(s for _, s in st) + 3 * len(st)}
                              for n, ti, h, st in TESTS]}
        if cmd == 'test.run':
            name = args.get('name', '')
            if not any(x[0] == name for x in TESTS):
                raise CmdError('test.unknown', f"no test '{name}'", 'test.list')
            if self.test_task or self.recording:
                raise CmdError('test.busy', 'a test or a recording is running', 'test.abort')
            self.test_task = asyncio.ensure_future(self.run_test(name))
            return {'test': name, 'started': True}
        if cmd == 'test.abort':
            if self.test_task:
                self.test_task.cancel()
            return {'aborted': True}
        if cmd == 'osc.config':
            return {'out': f"{args.get('out_host', '127.0.0.1')}:{args.get('out_port', 9001)}",
                    'in': args.get('in_port', 9000), 'on': args.get('on', True)}
        if cmd == 'log.tail':
            n = int(args.get('n', 50))
            return {'lines': self.log_lines[-n:], 'cmds': self.cmd_log[-n:]}
        raise CmdError('cmd.unknown', f"no command '{cmd}'", 'help lists them')


COMMANDS = ['help', 'status', 'connect', 'disconnect', 'stream', 'tune.list', 'tune.get', 'tune.set', 'tune.save',
            'tune.reset', 'tune.preset', 'tune.ab', 'settings.list', 'settings.step', 'key', 'joy.override',
            'inject.take', 'inject.gesture', 'inject.stop', 'record.start', 'record.stop', 'session.list',
            'session.info', 'session.analyze', 'session.replay', 'session.compare', 'test.list', 'test.run',
            'test.abort', 'audio.measure', 'screen.png', 'view.shot', 'plot', 'osc.config', 'curve', 'gestures',
            'log.tail']


class CmdError(Exception):
    def __init__(self, code, message, hint=''):
        super().__init__(message)
        self.code, self.message, self.hint = code, message, hint


class Client:
    def __init__(self, mock, writer):
        self.mock, self.writer, self.subs, self.name = mock, writer, set(), '?'

    def send(self, line, droppable=False):
        tr = self.writer.transport
        if tr.is_closing():
            return
        if droppable and tr.get_write_buffer_size() > 512 * 1024:      # a slow client: drop motion and state
            self.mock.drops['state'] += 1
            return
        self.writer.write(line)

    async def reply(self, rid, ok, result=None, error=None):
        msg = {'id': rid, 'ok': ok}
        if ok:
            msg['result'] = result
        else:
            msg['error'] = error
        self.send((json.dumps(msg, separators=(',', ':')) + '\n').encode())

    async def handle(self, req):
        rid, op = req.get('id'), req.get('op')
        m = self.mock
        if op == 'hello':
            self.name = req.get('client', '?')
            await self.reply(rid, True, {'daemon': 'slrig-mock', 'proto': 1, 'version': '0.0-mock',
                                         'streams': ['motion', 'state', 'screen', 'events', 'tune', 'settings',
                                                     'status', 'log', 'cmds', 'prompt', 'session']})
        elif op == 'subscribe':
            new = set(req.get('streams', [])) - self.subs
            self.subs |= set(req.get('streams', []))
            await self.reply(rid, True, {'streams': sorted(self.subs)})
            snap = {'tune': {'items': m.tune}, 'settings': {'items': settings_public(m.settings)}, 'status': m.status(),
                    'screen': m.screen_msg()}
            for ev, body in snap.items():
                if ev in new:
                    self.send((json.dumps({'ev': ev, **body}, separators=(',', ':')) + '\n').encode())
            if 'prompt' in new and m.prompt:
                self.send((json.dumps({'ev': 'prompt', **m.prompt}) + '\n').encode())
        elif op == 'unsubscribe':
            self.subs -= set(req.get('streams', []))
            await self.reply(rid, True, {'streams': sorted(self.subs)})
        elif op == 'run':
            cmd, args = req.get('cmd', ''), req.get('args') or {}
            t0 = time.monotonic()
            try:
                res = await m.run_cmd(self, cmd, args)
                await self.reply(rid, True, res)
                ok = True
            except CmdError as e:
                await self.reply(rid, False, error={'code': e.code, 'message': e.message, 'hint': e.hint})
                ok = False
            except (ValueError, TypeError) as e:
                await self.reply(rid, False, error={'code': 'args', 'message': str(e), 'hint': 'help ' + cmd})
                ok = False
            by = self.name if self.name in ('view', 'cli', 'mcp', 'osc') else 'cli'
            if cmd not in ('curve', 'gestures', 'tune.list', 'settings.list', 'session.list', 'test.list',
                           'log.tail', 'status', 'help') or by != 'view':
                await m.cmds_push(by, cmd, args, ok, int((time.monotonic() - t0) * 1000))
        else:
            await self.reply(rid, False, error={'code': 'op.unknown', 'message': f'no op {op!r}',
                                                'hint': 'hello, run, subscribe, unsubscribe'})


async def serve(path):
    mock = Mock()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        os.unlink(path)

    async def on_client(reader, writer):
        c = Client(mock, writer)
        mock.clients.add(c)
        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                try:
                    req = json.loads(line)
                except ValueError:
                    await c.reply(None, False, error={'code': 'json', 'message': 'not JSON', 'hint': ''})
                    continue
                await c.handle(req)
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        finally:
            mock.clients.discard(c)
            writer.close()

    server = await asyncio.start_unix_server(on_client, path=path, limit=1 << 20)
    os.chmod(path, 0o600)
    print(f'slrig mock daemon on {path} (pid {os.getpid()}), {len(mock.tune)} tuning values, '
          f'{len(mock.screens)} screens', flush=True)
    await mock.log('I (312) diag: boot: power on')
    await mock.log('I (318) main: Samplooler 0.1.0-rig, settings from flash, tuning v1')
    await mock.log('I (455) imu: MPU-6050 at 0x68, 500 Hz FIFO, gyro on')
    await mock.log('I (1020) rig: streams m q o e on')
    asyncio.ensure_future(mock.run_board())
    async with server:
        await server.serve_forever()


def send(path, text):
    """A one-shot client: hello as cli, the request, then print the reply."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    f = s.makefile('rwb')
    f.write(b'{"id": 0, "op": "hello", "client": "cli"}\n')
    req = json.loads(text)
    req.setdefault('id', 1)
    f.write((json.dumps(req) + '\n').encode())
    f.flush()
    while True:
        msg = json.loads(f.readline())
        if msg.get('id') == req['id']:
            print(json.dumps(msg, indent=1)[:4000])
            return 0 if msg.get('ok') else 1


def main():
    ap = argparse.ArgumentParser(description='slrig mock daemon (PROTOCOL.md section 2, made-up data)')
    ap.add_argument('--socket', default=default_socket())
    ap.add_argument('--send', metavar='JSON', help='send one request to a running daemon and print the reply')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    random.seed(a.seed)
    if a.send:
        return send(a.socket, a.send)
    try:
        asyncio.run(serve(a.socket))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
