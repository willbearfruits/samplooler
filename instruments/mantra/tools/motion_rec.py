#!/usr/bin/env python3
"""Record a player's real motion from a board, to replay in the simulator as a test (tools/sim/take.c).

The board streams what its instrument is given (the motion stream, docs/CORE.md): the 1 kHz accelerometer and gyro
in screen axes, REC and the knob. Only an instrument with INST.burst streams. Raw ALSA MIDI, no extra packages;
boards are found by their MIDI port's name (boards.py), never a TWOFOLD.

  motion_rec.py list                        the boards on USB, and the takes on disk
  motion_rec.py take NAME [--secs N] [--label JSON] [--prompt TEXT] [--board SERIAL | --all]
                                            start the stream, count down 3-2-1, record N seconds (10), stop, save
  motion_rec.py guided [--instrument ID] [--board SERIAL | --all] [--from NAME]
                                            the prompts in instruments/<ID>/tools/takes.json, one take each:
                                            Enter starts, r redoes, s skips, q stops
  motion_rec.py show FILE...                duration, samples, gaps, drops, peaks, and the motion drawn as a line
  motion_rec.py stop [--board SERIAL | --all]   stop a stream left running
  motion_rec.py convert IN.syx OUT.take     raw SysEx bytes (amidi -r, the simulator's out/motion-check.syx) to a take
  motion_rec.py verify IN.syx SOURCE.take   this decoder against messages the firmware's encoder made (tools/sim runs it)

--all records every board at once: each to its own file, the same take name, the same start.
Takes go to lab/takes/<instrument>/<YYYY-MM-DD>-<name>-<serial>.take beside the repository (MOTION_TAKES=<dir> puts
them elsewhere). A take is a JSON header line, then one line per sample: frame ax ay az gx gy gz flags (frame in 32 kHz
audio frames from the first sample, accel in g, gyro in deg/s; flags 1 REC down, 2 knob down, 4 gyro present,
8 packets lost just before this sample).
"""
import argparse, json, os, re, select, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import boards

SR = 32000                     # audio frames per second (core.h AUDIO_SR)
TICK = SR // 100               # frames per control tick, nominally
STEP = SR // 1000              # frames between samples at 1 kHz
ACC_LSB, GYRO_LSB = 1024.0, 4.0
F_REC, F_KNOB, F_GYRO, F_GAP = 1, 2, 4, 8
START = bytes([0xF0, 0x7D, 0x4D, 0x01, 0xF7])
STOP = bytes([0xF0, 0x7D, 0x4D, 0x00, 0xF7])
WHO = bytes([0xF0, 0x7D, ord('I'), 0xF7])
NEVER_USB = ('303a:4a57',)     # the TWOFOLD, by USB id as well as by name

HERE = os.path.dirname(os.path.abspath(__file__))


def repo():
    d = HERE
    while d != os.path.dirname(d):
        if os.path.isdir(os.path.join(d, 'instruments')) and os.path.isdir(os.path.join(d, 'core')):
            return d
        d = os.path.dirname(d)
    return None


def takes_dir():
    if os.environ.get('MOTION_TAKES'):
        return os.environ['MOTION_TAKES']
    r = repo()
    if not r:
        raise SystemExit('cannot find the repository from here: set MOTION_TAKES=<dir>')
    return os.path.join(os.path.dirname(r), 'lab', 'takes')


# ------------------------------------------------------------------ the messages
class Parser:
    """MIDI bytes in, whole SysEx bodies out (what sits between F0 and F7)."""
    def __init__(self):
        self.sx = None

    def feed(self, data):
        out = []
        for b in data:
            if b == 0xF0:
                self.sx = bytearray()
            elif b == 0xF7:
                if self.sx is not None:
                    out.append(bytes(self.sx))
                self.sx = None
            elif b >= 0xF8:
                continue                      # real-time bytes may sit anywhere
            elif b & 0x80:
                self.sx = None                # any other status ends a SysEx
            elif self.sx is not None:
                self.sx.append(b)
        return out


class Packet:
    __slots__ = ('seq', 'frames', 'flags', 'samples')

    def __init__(self, seq, frames, flags, samples):
        self.seq, self.frames, self.flags, self.samples = seq, frames, flags, samples


def s14(hi, lo):
    u = hi << 7 | lo
    return u - 16384 if u >= 8192 else u


def unpack(body):
    """A motion message's body (7D 6D seq frames*5 flags n samples) -> Packet, or None if it is not a whole one.
    Samples stay in steps: (ax, ay, az) in 1/1024 g, (gx, gy, gz) in 1/4 deg/s."""
    if len(body) < 10 or body[0] != 0x7D or body[1] != 0x6D:
        return None
    n = body[9]
    if len(body) != 10 + 12 * n or any(b & 0x80 for b in body):
        return None
    fr = 0
    for b in body[3:8]:
        fr = fr << 7 | b
    samples = []
    for i in range(n):
        p = 10 + 12 * i
        samples.append(tuple(s14(body[p + 2 * j], body[p + 2 * j + 1]) for j in range(6)))
    return Packet(body[2], fr & 0xFFFFFFFF, body[8], samples)


def pack(p):
    """Packet -> the whole message, F0 .. F7, as the firmware's front_motion_packet() writes it."""
    out = bytearray([0xF0, 0x7D, 0x6D, p.seq & 0x7F])
    out += bytes((p.frames >> s) & 0x7F for s in (28, 21, 14, 7, 0))
    out += bytes([p.flags & 0x7F, len(p.samples)])
    for smp in p.samples:
        for v in smp:
            w = v & 0x3FFF
            out += bytes([w >> 7, w & 0x7F])
    out.append(0xF7)
    return bytes(out)


def build(pkts):
    """Packets in the order they came -> (rows, stats). A row: [frame, ax, ay, az, gx, gy, gz, flags], values in
    steps, frame in the board's audio frames (64-bit, unwrapped). The tick's last sample sits at the tick's frame count;
    the others are spread evenly back to the tick before (1 kHz, 32 frames, after a gap, or when the spread would be
    off by more than a quarter). A gap (the sequence skipped: lost on the board or on the way) marks the next sample."""
    rows, gaps, lost_total = [], 0, 0
    prev_seq = prev_f = last = None
    gap_next = False
    for p in pkts:
        if prev_f is None:
            f, lost = p.frames, 0
        else:
            df = (p.frames - prev_f) & 0xFFFFFFFF
            if df >= 1 << 31:                 # frames went backwards: the board restarted
                df, lost = TICK, 1
            else:
                lost = (p.seq - prev_seq - 1) & 0x7F
                ticks = df / TICK
                if ticks > 100:               # more than the sequence can count: the frames tell
                    lost += 128 * max(0, round((ticks - 1 - lost) / 128))
            f = prev_f + df
        if lost:
            gaps += 1
            lost_total += lost
            gap_next = True
        n = len(p.samples)
        if n:
            sp = STEP
            if prev_f is not None and not lost and 24 * n <= f - prev_f <= 40 * n:
                sp = (f - prev_f) / n
            first = f - (n - 1) * sp
            if last is not None and not lost and first <= last:
                sp = (f - last) / n           # squeezed in after the tick before's last sample
            for i, smp in enumerate(p.samples):
                fl = p.flags & 7
                if gap_next:
                    fl |= F_GAP
                    gap_next = False
                rows.append([round(f - (n - 1 - i) * sp)] + list(smp) + [fl])
            last = rows[-1][0]
        prev_seq, prev_f = p.seq, f
    return rows, {'ticks': len(pkts), 'gaps': gaps, 'lost': lost_total}


def messages(data):
    """Raw MIDI bytes -> the motion packets in them, and how many motion messages were not whole."""
    pkts, bad = [], 0
    for body in Parser().feed(data):
        if len(body) >= 2 and body[0] == 0x7D and body[1] == 0x6D:
            p = unpack(body)
            if p:
                pkts.append(p)
            else:
                bad += 1
    return pkts, bad


# ------------------------------------------------------------------ take files
def write_take(path, header, rows):
    """rows in steps and board frames -> the file: frames from the first sample, g and deg/s."""
    f0 = rows[0][0] if rows else 0
    header = dict(header, frame0=f0, samples=len(rows), duration=round((rows[-1][0] - f0) / SR, 4) if rows else 0)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + '.part'
    with open(tmp, 'w') as f:
        f.write(json.dumps(header) + '\n')
        for r in rows:
            f.write(f'{r[0] - f0} {r[1] / ACC_LSB:.5f} {r[2] / ACC_LSB:.5f} {r[3] / ACC_LSB:.5f} '
                    f'{r[4] / GYRO_LSB:.2f} {r[5] / GYRO_LSB:.2f} {r[6] / GYRO_LSB:.2f} {r[7]}\n')
    os.replace(tmp, path)
    return header


def read_take(path):
    """-> (header dict, rows [frame, ax, ay, az, gx, gy, gz, flags] in g and deg/s)"""
    header, rows = {}, []
    with open(path) as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith('#'):
                continue
            if s.startswith('{'):
                header = json.loads(s)
                continue
            v = s.split()
            rows.append([int(v[0])] + [float(x) for x in v[1:7]] + [int(v[7])])
    return header, rows


def header_for(info, board, name, prompt, label, secs, go_wall, stats, counts):
    return {
        'format': 'samplooler-take/1',
        'instrument': info.get('name') or board.name,
        'version': info.get('version', ''),
        'serial': info.get('serial') or board.serial,
        'date': time.strftime('%Y-%m-%dT%H:%M:%S', time.localtime(go_wall)),
        'take': name, 'prompt': prompt, 'label': label,
        'rate': 1000, 'axes': 'screen',
        'units': {'frame': 'audio frames at 32000 Hz from the first sample', 'acc': 'g', 'gyro': 'deg/s'},
        'columns': ['frame', 'ax', 'ay', 'az', 'gx', 'gy', 'gz', 'flags'],
        'flag_bits': {'1': 'REC down', '2': 'knob down', '4': 'gyro present', '8': 'packets lost just before'},
        'secs': secs, 'go': round(go_wall, 3),
        'ticks': stats['ticks'], 'gaps': stats['gaps'], 'lost': stats['lost'],
        'board_sent': counts[0], 'board_dropped': counts[1],
    }


# ------------------------------------------------------------------ the boards
def usb_id(dev):
    m = re.search(r'midiC(\d+)D', dev)
    if not m:
        return ''
    base = f'/sys/class/sound/card{m.group(1)}/device/..'
    try:
        return open(base + '/idVendor').read().strip() + ':' + open(base + '/idProduct').read().strip()
    except OSError:
        return ''


def family():
    return [b for b in boards.boards() if usb_id(b[1]) not in NEVER_USB]


def pick(args):
    bs = family()
    want = getattr(args, 'board', None) or os.environ.get('BOARD')
    if want:
        bs = [b for b in bs if want.upper().replace(':', '') in b[2].upper().replace(':', '')]
    if not bs:
        raise SystemExit('no family board on USB' + (f' with {want} in its serial' if want else ''))
    if getattr(args, 'all', False) or len(bs) == 1:
        return bs
    raise SystemExit('more than one board: ' + ', '.join(f'{b[0]} {b[2]}' for b in bs) + ' (--board SERIAL or --all)')


def bump_buffer(fd, size=1 << 16):
    """Ask ALSA for a 64 KB input buffer (5 s of the stream) instead of 4 KB; harmless if it says no."""
    try:
        import ctypes, fcntl

        class Params(ctypes.Structure):    # struct snd_rawmidi_params
            _fields_ = [('stream', ctypes.c_int), ('buffer_size', ctypes.c_size_t), ('avail_min', ctypes.c_size_t),
                        ('flags', ctypes.c_uint), ('mode', ctypes.c_uint), ('reserved', ctypes.c_ubyte * 12)]
        p = Params(1, size, 1, 0, 0)       # SNDRV_RAWMIDI_STREAM_INPUT
        fcntl.ioctl(fd, (3 << 30) | (ctypes.sizeof(Params) << 16) | (ord('W') << 8) | 0x10, p)
        return True
    except (OSError, ImportError, ValueError):
        return False


class Board:
    """One board's MIDI port, read by a thread of its own so nothing waits on the terminal."""
    def __init__(self, name, dev, serial):
        self.name, self.dev, self.serial = name, dev, serial
        try:
            self.fd = os.open(dev, os.O_RDWR | os.O_NONBLOCK)
        except OSError as e:
            raise SystemExit(f'{name} {serial}: cannot open {dev} ({e.strerror}): is the app or sl_midi.py log using it?')
        bump_buffer(self.fd)
        self.lock = threading.Lock()
        self.msgs, self.logs, self.info = [], [], {}
        self.keep, self.count, self.alive, self.err = False, 0, True, None
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        parser = Parser()
        while self.alive:
            try:
                if not select.select([self.fd], [], [], 0.1)[0]:
                    continue
                data = os.read(self.fd, 1 << 16)
            except BlockingIOError:
                continue
            except OSError as e:
                self.err = e.strerror
                return
            t = time.monotonic()
            for body in parser.feed(data):
                if len(body) < 2 or body[0] != 0x7D:
                    continue
                if body[1] == 0x6D:
                    with self.lock:
                        self.count += 1
                        if self.keep:
                            self.msgs.append((t, body))
                elif body[1] == ord('L'):
                    with self.lock:
                        self.logs.append((t, body[2:].decode('ascii', 'replace')))
                elif body[1] == ord('I'):
                    text = body[2:].decode('ascii', 'replace')
                    self.info = dict(l.split('=', 1) for l in text.split('\n') if '=' in l)

    def send(self, b):
        for _ in range(100):
            try:
                os.write(self.fd, b)
                return
            except BlockingIOError:
                time.sleep(0.002)
        raise SystemExit(f'{self.name}: the port takes nothing')

    def log_after(self, t, pattern):
        with self.lock:
            for tt, text in reversed(self.logs):
                if tt >= t:
                    m = re.search(pattern, text)
                    if m:
                        return m
        return None

    def close(self):
        self.alive = False
        self.thread.join(0.5)
        os.close(self.fd)


def open_boards(found):
    return [Board(*b) for b in found]


def stop_all(bs):
    for b in bs:
        try:
            b.send(STOP)
        except (SystemExit, OSError):
            pass


# ------------------------------------------------------------------ recording
def slug(s):
    return re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-') or 'take'


def take_path(instrument, name, serial, when):
    day = time.strftime('%Y-%m-%d', time.localtime(when))
    serial = re.sub(r'[^A-Za-z0-9-]', '', serial or '') or 'noserial'
    return os.path.join(takes_dir(), slug(instrument), f'{day}-{slug(name)}-{serial}.take')


def record(bs, name, secs, label=None, prompt=''):
    """One take from every board in bs, all started together. -> [(path, header)]"""
    t_ask = time.monotonic()
    for b in bs:
        b.info = {}
        b.send(WHO)
    while time.monotonic() - t_ask < 0.6 and not all(b.info for b in bs):
        time.sleep(0.02)
    for b in bs:
        with b.lock:
            b.count, b.msgs, b.keep = 0, [], False
    t_start = time.monotonic()
    try:
        for b in bs:
            b.send(START)
        while time.monotonic() - t_start < 1.0 and not all(b.count for b in bs):
            time.sleep(0.02)
        live = []
        for b in bs:
            if b.count:
                live.append(b)
                continue
            why = b.log_after(t_start, r'motion: .*no stream')
            print(f'  {b.name} {b.serial}: ' + ('its instrument has no 1 kHz motion (INST.burst)' if why else
                  'no motion stream: its firmware is older than the stream, or it is not one of the instruments'))
        if not live:
            raise SystemExit('nothing to record')
        for k in (3, 2, 1):
            print(f'\r  {k} ', end='', flush=True)
            time.sleep(1.0)
        go_wall, t_go = time.time(), time.monotonic()
        for b in live:
            with b.lock:
                b.msgs, b.keep = [], True
        print('\r  GO' + ' ' * 20)
        while (el := time.monotonic() - t_go) < secs:
            parts = []
            for b in live:
                with b.lock:
                    n = len(b.msgs)
                parts.append(f'{b.name} {b.serial[-8:]}: {n} ticks')
            print(f'\r  {el:5.1f} / {secs:g} s   ' + '   '.join(parts) + '   ', end='', flush=True)
            time.sleep(0.1)
        for b in live:
            with b.lock:
                b.keep = False
        print(f'\r  {secs:5.1f} / {secs:g} s   done' + ' ' * 40)
    finally:
        t_stop = time.monotonic()
        stop_all(bs)
    time.sleep(0.4)                           # the board's count comes back as a log line
    out = []
    for b in live:
        with b.lock:
            msgs = list(b.msgs)
        pkts = [p for p in (unpack(m) for _, m in msgs) if p]
        bad = len(msgs) - len(pkts)
        rows, stats = build(pkts)
        stats['lost'] += bad
        m = b.log_after(t_stop - 0.5, r'motion: off, (\d+) sent, (\d+) dropped')
        counts = (int(m.group(1)), int(m.group(2))) if m else (None, None)
        if b.err:
            print(f'  {b.name}: the port failed ({b.err})')
        if not rows:
            print(f'  {b.name} {b.serial}: no samples came: nothing saved')
            continue
        header = header_for(b.info, b, name, prompt, label if label is not None else {}, secs, go_wall, stats, counts)
        path = take_path(header['instrument'], name, header['serial'], go_wall)
        header = write_take(path, header, rows)
        out.append((path, header))
    return out


# ------------------------------------------------------------------ showing a take
BARS = ' ▁▂▃▄▅▆▇█'


def spark(values, width=64):
    if not values:
        return ''
    top = max(values) or 1.0
    per = len(values) / width
    out = []
    for i in range(min(width, len(values))):
        chunk = values[int(i * per):max(int(i * per) + 1, int((i + 1) * per))]
        out.append(BARS[min(8, int(round(max(chunk) / top * 8)))])
    return ''.join(out)


def show(path):
    h, rows = read_take(path)
    n = len(rows)
    dur = rows[-1][0] / SR if rows else 0.0
    print(f'{os.path.basename(path)}')
    print(f'  {h.get("take", "?")}: {h.get("instrument", "?")} {h.get("serial", "")} {h.get("version", "")}, {h.get("date", "")}')
    if h.get('prompt'):
        print(f'  "{h["prompt"]}"')
    print(f'  label {json.dumps(h.get("label", {}))}')
    if not rows:
        print('  no samples')
        return
    gaps = sum(1 for r in rows if r[7] & F_GAP)
    drops = h.get('board_dropped')
    print(f'  {dur:.2f} s, {n} samples ({n / dur if dur else 0:.0f} a second), {h.get("ticks", "?")} ticks, '
          f'{gaps} gaps ({h.get("lost", 0)} messages lost), the board dropped {drops if drops is not None else "?"}')
    g = [(r[4] ** 2 + r[5] ** 2 + r[6] ** 2) ** 0.5 for r in rows]
    a = [(r[1] ** 2 + r[2] ** 2 + r[3] ** 2) ** 0.5 for r in rows]
    peak_axis = lambda j: max(abs(r[j]) for r in rows)
    print(f'  peak gyro {max(g):.0f} deg/s (x {peak_axis(4):.0f}, y {peak_axis(5):.0f}, z {peak_axis(6):.0f})   '
          f'peak accel {max(a):.2f} g (x {peak_axis(1):.2f}, y {peak_axis(2):.2f}, z {peak_axis(3):.2f})')
    rec = sum(1 for r in rows if r[7] & F_REC) / 1000.0
    knob = sum(1 for r in rows if r[7] & F_KNOB) / 1000.0
    nogyro = sum(1 for r in rows if not r[7] & F_GYRO)
    print(f'  REC down {rec:.1f} s, knob down {knob:.1f} s' + (f', {nogyro} samples without the gyro' if nogyro else ''))
    print(f'  gyro  |{spark(g)}|')
    print(f'  accel |{spark([abs(x - 1.0) for x in a])}|  (|a| - 1 g)')


# ------------------------------------------------------------------ verify: the C encoder's bytes, this decoder
def verify(syx, src):
    data = open(syx, 'rb').read()
    bodies = [b for b in Parser().feed(data) if len(b) >= 2 and b[0] == 0x7D and b[1] == 0x6D]
    pkts = [unpack(b) for b in bodies]
    fails = 0

    def say(ok, text):
        nonlocal fails
        fails += not ok
        print(f'  {"ok  " if ok else "FAIL"}  {text}')

    whole = [p for p in pkts if p]
    say(bodies and len(whole) == len(bodies), f'{len(bodies)} messages, {len(whole)} whole')
    same = sum(pack(p) == bytes([0xF0]) + b + bytes([0xF7]) for p, b in zip(pkts, bodies) if p)
    say(same == len(bodies), f'unpacked and packed again, {same} of {len(bodies)} are the same bytes')
    rows, st = build(whole)
    h, ref = read_take(src)
    say(len(rows) == len(ref) and st['gaps'] == 0, f'{len(rows)} samples (the source has {len(ref)}), {st["gaps"]} gaps')
    if rows and len(rows) == len(ref):
        f0 = rows[0][0]
        df = max(abs((r[0] - f0) - s[0]) for r, s in zip(rows, ref))
        ea = max(abs(r[j] / ACC_LSB - s[j]) * ACC_LSB for r, s in zip(rows, ref) for j in (1, 2, 3))
        withg = [(r, s) for r, s in zip(rows, ref) if r[7] & F_GYRO]
        eg = max((abs(r[j] / GYRO_LSB - s[j]) * GYRO_LSB for r, s in withg for j in (4, 5, 6)), default=0.0)
        fl = sum((r[7] & 3) != (s[7] & 3) for r, s in zip(rows, ref))
        say(df == 0, f'frames from the first sample match the source (worst {df} frames off)')
        say(ea <= 0.51 and eg <= 0.51, f'values within half a step: accel {ea:.3f}, gyro {eg:.3f} steps '
            f'({len(withg)} samples with the gyro)')
        say(fl == 0, f'REC and knob flags match ({fl} differ)')
        tmp = os.path.splitext(syx)[0] + '-decoded.take'
        write_take(tmp, {'take': 'verify'}, rows)
        _, back = read_take(tmp)
        eb = max(abs(b[j] - (r[j] / ACC_LSB if j < 4 else r[j] / GYRO_LSB)) for b, r in zip(back, rows) for j in range(1, 7))
        say(len(back) == len(rows) and eb < 1e-5 and all(b[0] == r[0] - f0 for b, r in zip(back, rows)),
            f'written and read back ({os.path.basename(tmp)}): the same (worst {eb:.1g})')
        # three messages lost on the way: one gap, marked, and nothing after it moves
        nonempty = [i for i, p in enumerate(whole) if p.samples]
        if len(nonempty) > 10:
            cut = nonempty[5]
            lost_rows = sum(len(p.samples) for p in whole[cut:cut + 3])
            rows2, st2 = build(whole[:cut] + whole[cut + 3:])
            keep = rows[:sum(len(p.samples) for p in whole[:cut])] + rows[sum(len(p.samples) for p in whole[:cut + 3]):]
            marked = [i for i, r in enumerate(rows2) if r[7] & F_GAP]
            say(st2['gaps'] == 1 and st2['lost'] == 3 and len(rows2) == len(rows) - lost_rows and len(marked) == 1
                and all(a[0] == b[0] for a, b in zip(rows2, keep)),
                f'3 messages lost: {st2["gaps"]} gap of {st2["lost"]}, the next sample marked, the frames after it unmoved')
        # the sequence and the frame count wrap
        moved = [Packet((p.seq + 100) & 0x7F, (p.frames + (1 << 32) - 5000) & 0xFFFFFFFF, p.flags, p.samples) for p in whole]
        rows3, st3 = build(moved)
        say(st3['gaps'] == 0 and len(rows3) == len(rows) and all(a[0] - rows3[0][0] == b[0] - f0 for a, b in zip(rows3, rows)),
            'the sequence and the 32-bit frame count wrapping mid-take change nothing')
    return 0 if fails == 0 else 1


# ------------------------------------------------------------------ commands
def cmd_list(_):
    bs = boards.boards()
    print('boards on USB:' if bs else 'no family board on USB')
    for name, dev, serial in bs:
        skip = usb_id(dev) in NEVER_USB
        print(f'  {name:12} {serial or "(no serial)":14} {dev}' + ('   (never touched)' if skip else ''))
    d = takes_dir()
    print(f'takes in {d}:')
    if not os.path.isdir(d):
        print('  none yet')
        return 0
    for inst in sorted(os.listdir(d)):
        fs = sorted(f for f in os.listdir(os.path.join(d, inst)) if f.endswith('.take'))
        if fs:
            print(f'  {inst:12} {len(fs):3} takes, the latest {fs[-1]}')
    return 0


def cmd_take(args):
    label = {}
    if args.label:
        try:
            label = json.loads(args.label)
        except json.JSONDecodeError as e:
            raise SystemExit(f'--label is not JSON: {e}')
    bs = open_boards(pick(args))
    try:
        for path, h in record(bs, args.name, args.secs, label, args.prompt or ''):
            print(f'  saved {path}')
            show(path)
    except KeyboardInterrupt:
        print('\n  stopped: nothing saved')
        return 1
    finally:
        stop_all(bs)
        for b in bs:
            b.close()
    return 0


def load_prompts(instrument):
    r = repo()
    p = os.path.join(r, 'instruments', instrument, 'tools', 'takes.json') if r else ''
    if not os.path.isfile(p):
        raise SystemExit(f'no prompts for {instrument}: {p or "(no repository found)"}')
    t = json.load(open(p))
    return t['takes'] if isinstance(t, dict) else t


def cmd_guided(args):
    found = pick(args)
    instrument = (args.instrument or found[0][0]).lower()
    prompts = load_prompts(instrument)
    for f in found:
        if f[0].lower() != instrument:
            print(f'  note: {f[2]} runs {f[0]}, the prompts are {instrument.upper()}\'s')
    if args.start:
        names = [p['name'] for p in prompts]
        if args.start not in names:
            raise SystemExit(f'no prompt {args.start}: ' + ', '.join(names))
        prompts = prompts[names.index(args.start):]
    bs = open_boards(found)
    try:
        for i, p in enumerate(prompts):
            while True:
                print(f'\n({i + 1}/{len(prompts)}) {p["name"].upper()}, {p.get("secs", 10)} s')
                print(f'  {p["prompt"]}')
                a = input('  Enter: record   s: skip   q: stop > ').strip().lower()
                if a == 'q':
                    return 0
                if a == 's':
                    break
                saved = record(bs, p['name'], float(p.get('secs', 10)), p.get('label', {}), p['prompt'])
                for path, _ in saved:
                    show(path)
                a = input('  Enter: next   r: redo   q: stop > ').strip().lower()
                if a == 'r':
                    continue
                if a == 'q':
                    return 0
                break
        print('\nall done')
    except (KeyboardInterrupt, EOFError):
        print('\n  stopped')
        return 1
    finally:
        stop_all(bs)
        for b in bs:
            b.close()
    return 0


def cmd_stop(args):
    bs = open_boards(pick(args))
    stop_all(bs)
    time.sleep(0.3)
    for b in bs:
        m = b.log_after(0, r'motion: off, (\d+) sent, (\d+) dropped')
        print(f'  {b.name} {b.serial}: ' + (f'stopped ({m.group(1)} sent, {m.group(2)} dropped)' if m else 'told to stop'))
        b.close()
    return 0


def cmd_convert(args):
    pkts, bad = messages(open(args.syx, 'rb').read())
    rows, st = build(pkts)
    st['lost'] += bad
    if not rows:
        raise SystemExit('no motion samples in ' + args.syx)
    h = {
        'format': 'samplooler-take/1', 'take': os.path.splitext(os.path.basename(args.out))[0], 'source': args.syx,
        'rate': 1000, 'axes': 'screen', 'label': {},
        'units': {'frame': 'audio frames at 32000 Hz from the first sample', 'acc': 'g', 'gyro': 'deg/s'},
        'ticks': st['ticks'], 'gaps': st['gaps'], 'lost': st['lost']}
    write_take(args.out, h, rows)
    print(f'{args.out}: {len(rows)} samples from {st["ticks"]} messages, {st["gaps"]} gaps ({st["lost"]} lost)')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('list')
    p = sub.add_parser('take')
    p.add_argument('name')
    p.add_argument('--secs', type=float, default=10.0)
    p.add_argument('--label', help='JSON the replay test can check, e.g. \'{"hits": {"LEFT": 10}, "others": 0}\'')
    p.add_argument('--prompt', help='what the player was asked to do')
    for q in (p, sub.add_parser('guided'), sub.add_parser('stop')):
        g = q.add_mutually_exclusive_group()
        g.add_argument('--board', help='part of the serial (as BOARD=)')
        g.add_argument('--all', action='store_true', help='every family board at once')
    sub.choices['guided'].add_argument('--instrument', help='whose prompts (default: the board\'s)')
    sub.choices['guided'].add_argument('--from', dest='start', help='start at this prompt')
    p = sub.add_parser('show')
    p.add_argument('files', nargs='+')
    p = sub.add_parser('convert')
    p.add_argument('syx')
    p.add_argument('out')
    p = sub.add_parser('verify')
    p.add_argument('syx')
    p.add_argument('src')
    a = ap.parse_args()
    if a.cmd == 'list':
        return cmd_list(a)
    if a.cmd == 'take':
        return cmd_take(a)
    if a.cmd == 'guided':
        return cmd_guided(a)
    if a.cmd == 'stop':
        return cmd_stop(a)
    if a.cmd == 'show':
        for f in a.files:
            show(f)
        return 0
    if a.cmd == 'convert':
        return cmd_convert(a)
    if a.cmd == 'verify':
        return verify(a.syx, a.src)
    return 1


if __name__ == '__main__':
    sys.exit(main())
