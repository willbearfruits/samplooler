"""OSC over UDP, stdlib only: the rig's streams out (to Max, Pd, TouchDesigner, a synth), commands in.

Out (default 127.0.0.1:9000):
  /samplooler/motion  ax ay az gx gy gz        device axes, g and deg/s, the latest sample, at most 100 a second
  /samplooler/angles  pitch roll yaw           degrees, the board's (pitch + nose down)
  /samplooler/quat    w x y z
  /samplooler/speed   cmd now
  /samplooler/fx      delay stutter
  /samplooler/state   state mode               names ("play", "TAPE")
  /samplooler/event   name v ms text           every gesture as it fires
In (default port 9001):
  /samplooler/tune/<name> f      tune.set        /samplooler/setting/<index> i (+1 or -1)   settings.step
  /samplooler/rec i (1 down, 0 up)               /samplooler/enc i (detents)   /samplooler/switch i
  /samplooler/run s [s]          any command, with its arguments as a JSON object
"""
from __future__ import annotations

import json
import socket
import struct
import threading
from typing import Any, Callable


def _pad(b: bytes) -> bytes:
    return b + b'\0' * (4 - len(b) % 4)


def encode(addr: str, *args: Any) -> bytes:
    tags, data = ',', b''
    for a in args:
        if isinstance(a, bool) or isinstance(a, int):
            tags += 'i'; data += struct.pack('>i', int(a))
        elif isinstance(a, float):
            tags += 'f'; data += struct.pack('>f', a)
        else:
            tags += 's'; data += _pad(str(a).encode())
    return _pad(addr.encode()) + _pad(tags.encode()) + data


def decode(pkt: bytes) -> tuple[str, list]:
    def string(i):
        j = pkt.index(b'\0', i)
        return pkt[i:j].decode(errors='replace'), (j + 4) & ~3
    addr, i = string(0)
    if i >= len(pkt):
        return addr, []
    tags, i = string(i)
    out = []
    for t in tags[1:]:
        if t == 'i':
            out.append(struct.unpack('>i', pkt[i:i + 4])[0]); i += 4
        elif t == 'f':
            out.append(struct.unpack('>f', pkt[i:i + 4])[0]); i += 4
        elif t == 's':
            s, i = string(i); out.append(s)
        elif t in 'TF':
            out.append(t == 'T')
    return addr, out


class Osc:
    def __init__(self, run: Callable[[str, dict, str], dict]):
        self.run = run
        self.out_host, self.out_port, self.in_port = '127.0.0.1', 9000, 9001
        self.on = True
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx: socket.socket | None = None
        self.sent = self.received = 0
        self.error = ''
        self._listen()

    def _listen(self) -> None:
        if self.rx:
            try:
                self.rx.close()
            except OSError:
                pass
            self.rx = None
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.bind(('127.0.0.1', self.in_port))
            self.rx = s
            self.error = ''
            threading.Thread(target=self._read, args=(s,), name='osc-in', daemon=True).start()
        except OSError as e:
            self.error = f'cannot listen on {self.in_port}: {e.strerror}'

    def configure(self, out_host: str | None = None, out_port: int | None = None, in_port: int | None = None,
                  on: bool | None = None) -> dict:
        if out_host:
            self.out_host = out_host
        if out_port:
            self.out_port = int(out_port)
        if on is not None:
            self.on = bool(on)
        if in_port and int(in_port) != self.in_port:
            self.in_port = int(in_port)
            self._listen()
        return self.info()

    def info(self) -> dict:
        return {'on': self.on, 'out': f'{self.out_host}:{self.out_port}', 'in': self.in_port, 'sent': self.sent,
                'received': self.received, 'error': self.error}

    def send(self, addr: str, *args: Any) -> None:
        if not self.on:
            return
        try:
            self.tx.sendto(encode(addr, *args), (self.out_host, self.out_port))
            self.sent += 1
        except OSError:
            pass

    def _read(self, s: socket.socket) -> None:
        while True:
            try:
                pkt, _ = s.recvfrom(4096)
            except OSError:
                return
            try:
                addr, args = decode(pkt)
            except (ValueError, struct.error, IndexError):
                continue
            self.received += 1
            self._handle(addr, args)

    def _handle(self, addr: str, args: list) -> None:
        p = addr.strip('/').split('/')
        if not p or p[0] != 'samplooler':
            return
        a0 = args[0] if args else None
        if len(p) == 3 and p[1] == 'tune' and a0 is not None:
            self.run('tune.set', {'name': p[2], 'value': float(a0)}, 'osc')
        elif len(p) == 3 and p[1] == 'setting' and a0 is not None:
            self.run('settings.step', {'index': int(p[2]), 'delta': int(a0)}, 'osc')
        elif p[1:] == ['rec'] and a0 is not None:
            self.run('key', {'rec': bool(a0)}, 'osc')
        elif p[1:] == ['enc'] and a0 is not None:
            self.run('key', {'enc': int(a0)}, 'osc')
        elif p[1:] == ['switch'] and a0 is not None:
            self.run('key', {'enc_down': bool(a0)}, 'osc')
        elif p[1:] == ['run'] and isinstance(a0, str):
            try:
                kw = json.loads(args[1]) if len(args) > 1 else {}
            except ValueError:
                kw = {}
            self.run(a0, kw, 'osc')
