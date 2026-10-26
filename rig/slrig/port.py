"""Where the bytes come from: a Samplooler's raw ALSA MIDI port, or the twin (rig/twin) as a child process.
A board is found by its MIDI port's name, as core/tools/boards.py does; a TWOFOLD is never opened, by name or by USB id."""
from __future__ import annotations

import glob
import os
import select
import shutil
import subprocess
import threading
import time
from pathlib import Path
from typing import Callable

NAMES = ('Samplooler',)
NEVER_NAMES = ('TWOFOLD',)
NEVER_USB = ('303a:4a57',)
RIG = Path(__file__).resolve().parent.parent


def _usb_id(card: str) -> str:
    base = f'/sys/class/sound/card{card}/device/..'
    try:
        return open(base + '/idVendor').read().strip() + ':' + open(base + '/idProduct').read().strip()
    except OSError:
        return ''


def boards() -> list[dict]:
    """Every Samplooler on USB: {name, dev, serial, usb, card}. Nothing else is listed, and never a TWOFOLD."""
    out = []
    for path in sorted(glob.glob('/proc/asound/card*/midi0')):
        try:
            first = open(path).readline().strip()
        except OSError:
            continue
        card = path.split('card')[1].split('/')[0]
        usb = _usb_id(card)
        if any(x in first for x in NEVER_NAMES) or usb in NEVER_USB:
            continue
        if not any(first == n or first.startswith(n + ' ') for n in NAMES):
            continue
        serial = ''
        try:
            serial = open(f'/sys/class/sound/card{card}/device/../serial').read().strip()
        except OSError:
            pass
        out.append({'name': first, 'dev': f'/dev/snd/midiC{card}D0', 'serial': serial, 'usb': usb, 'card': card})
    return out


def _bump_buffer(fd: int, size: int = 1 << 18) -> bool:
    """A 256 KB ALSA input buffer instead of 4 KB: the streams are ~40 KB/s (motion_rec.py's ioctl)."""
    try:
        import ctypes
        import fcntl

        class Params(ctypes.Structure):    # struct snd_rawmidi_params
            _fields_ = [('stream', ctypes.c_int), ('buffer_size', ctypes.c_size_t), ('avail_min', ctypes.c_size_t),
                        ('flags', ctypes.c_uint), ('mode', ctypes.c_uint), ('reserved', ctypes.c_ubyte * 12)]
        p = Params(1, size, 1, 0, 0)       # SNDRV_RAWMIDI_STREAM_INPUT
        fcntl.ioctl(fd, (3 << 30) | (ctypes.sizeof(Params) << 16) | (ord('W') << 8) | 0x10, p)
        return True
    except (OSError, ImportError, ValueError):
        return False


class Port:
    """A byte pipe with a reader thread: on_bytes(data) is called from that thread."""
    kind = 'none'

    def __init__(self, on_bytes: Callable[[bytes], None], on_close: Callable[[str], None]):
        self.on_bytes, self.on_close = on_bytes, on_close
        self.alive = True
        self.wlock = threading.Lock()

    def write(self, data: bytes) -> None:
        raise NotImplementedError

    def close(self) -> None:
        self.alive = False


class BoardPort(Port):
    kind = 'board'

    def __init__(self, board: dict, on_bytes, on_close):
        super().__init__(on_bytes, on_close)
        if board.get('usb') in NEVER_USB or any(x in board.get('name', '') for x in NEVER_NAMES):
            raise PermissionError('never a TWOFOLD')
        self.board = board
        self.fd = os.open(board['dev'], os.O_RDWR | os.O_NONBLOCK)
        _bump_buffer(self.fd)
        self.thread = threading.Thread(target=self._read, name='board-read', daemon=True)
        self.thread.start()

    def _read(self):
        while self.alive:
            try:
                if not select.select([self.fd], [], [], 0.1)[0]:
                    continue
                data = os.read(self.fd, 1 << 16)
            except BlockingIOError:
                continue
            except OSError as e:
                if self.alive:
                    self.alive = False
                    self.on_close(f'the board went away ({e.strerror})')
                return
            if data:
                self.on_bytes(data)

    def write(self, data: bytes) -> None:
        with self.wlock:
            k = 0
            deadline = time.monotonic() + 1.0
            while k < len(data):
                try:
                    k += os.write(self.fd, data[k:])
                except BlockingIOError:
                    if time.monotonic() > deadline:
                        raise TimeoutError('the board takes nothing')
                    time.sleep(0.001)

    def close(self) -> None:
        self.alive = False
        try:
            self.thread.join(0.5)
            os.close(self.fd)
        except OSError:
            pass


def twin_binary() -> Path:
    return RIG / 'twin' / 'build' / 'twin'


def ensure_twin() -> Path:
    b = twin_binary()
    if not b.exists():
        subprocess.run(['sh', str(RIG / 'twin' / 'build.sh')], check=True, capture_output=True)
    return b


class TwinPort(Port):
    kind = 'twin'

    def __init__(self, on_bytes, on_close, scene: str = 'still', log_path: Path | None = None, wav_path: Path | None = None):
        super().__init__(on_bytes, on_close)
        exe = ensure_twin()
        self.log = open(log_path, 'ab') if log_path else subprocess.DEVNULL
        extra = ['--wav', str(wav_path)] if wav_path else []
        self.proc = subprocess.Popen([str(exe), '--scene', scene] + extra, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.log, bufsize=0)
        self.scene = scene
        self.board = {'name': 'Samplooler twin', 'dev': 'twin', 'serial': 'TWIN', 'usb': '', 'card': ''}
        self.thread = threading.Thread(target=self._read, name='twin-read', daemon=True)
        self.thread.start()

    def _read(self):
        out = self.proc.stdout
        while self.alive:
            data = out.read1(1 << 16) if hasattr(out, 'read1') else os.read(out.fileno(), 1 << 16)
            if not data:
                if self.alive:
                    self.alive = False
                    self.on_close('the twin stopped')
                return
            self.on_bytes(data)

    def write(self, data: bytes) -> None:
        with self.wlock:
            try:
                self.proc.stdin.write(data)
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                pass

    def close(self) -> None:
        self.alive = False
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(1.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()


class Calc:
    """The twin's calculator (twin --calc): the tilt curve and replays, by the firmware's own code."""

    def __init__(self):
        self.proc = subprocess.Popen([str(ensure_twin()), '--calc'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, bufsize=1)
        self.lock = threading.Lock()

    def ask(self, line: str) -> str:
        with self.lock:
            self.proc.stdin.write(line.rstrip('\n') + '\n')
            self.proc.stdin.flush()
            return self.proc.stdout.readline().strip()

    def load_tune(self, items: list[dict]) -> None:
        self.ask('defaults')
        for it in items:
            if it.get('kind') != 'action' and it.get('value') != it.get('def'):
                self.ask(f"set {it['i']} {it['value']}")

    def close(self):
        try:
            self.ask('quit')
        except (BrokenPipeError, OSError):
            pass
        self.proc.kill()


def which(cmd: str) -> str | None:
    return shutil.which(cmd)
