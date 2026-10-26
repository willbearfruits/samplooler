"""A client of slrigd (the CLI's and the MCP server's): connects, starting the daemon when none runs, and runs commands."""
from __future__ import annotations

import itertools
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

from .daemon import runtime_dir, socket_path


def start_daemon(source: str = 'auto', scene: str = 'still', wait: float = 8.0) -> Path:
    """slrigd in the background, in its own session (it outlives the caller); its log in the run folder."""
    path = socket_path()
    log = open(runtime_dir() / 'daemon.log', 'ab')
    subprocess.Popen([sys.executable, '-m', 'slrig.cli', 'serve', '--source', source, '--scene', scene],
                     stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True,
                     cwd=str(Path(__file__).resolve().parent.parent), env=dict(os.environ))
    end = time.monotonic() + wait
    while time.monotonic() < end:
        if path.exists():
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(str(path))
                s.close()
                return path
            except OSError:
                pass
        time.sleep(0.1)
    raise RuntimeError(f'slrigd did not start (see {runtime_dir() / "daemon.log"})')


class Client:
    def __init__(self, name: str = 'cli', autostart: bool = True, source: str = 'auto'):
        self.name = name
        path = socket_path()
        try:
            self.sock = self._connect(path)
        except OSError:
            if not autostart:
                raise
            start_daemon(source)
            self.sock = self._connect(path)
        self.f = self.sock.makefile('r', encoding='utf-8')
        self.ids = itertools.count(1)
        self.pushes: list[dict] = []
        self.request('hello', client=name)

    @staticmethod
    def _connect(path: Path) -> socket.socket:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(str(path))
        return s

    def request(self, op: str, timeout: float = 600.0, **kw) -> dict:
        rid = next(self.ids)
        self.sock.sendall((json.dumps(dict(kw, id=rid, op=op)) + '\n').encode())
        self.sock.settimeout(timeout)
        while True:
            line = self.f.readline()
            if not line:
                raise ConnectionError('slrigd closed the connection')
            msg = json.loads(line)
            if msg.get('id') == rid:
                return msg
            if 'ev' in msg:
                self.pushes.append(msg)
                del self.pushes[:-200]

    def run(self, cmd: str, args: dict | None = None, timeout: float = 600.0) -> dict:
        return self.request('run', timeout=timeout, cmd=cmd, args=args or {})

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass
