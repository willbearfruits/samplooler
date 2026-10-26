"""slrigd: the rig's hub. It owns the port (board or twin), keeps the model (rig.py) and serves it on a unix socket,
JSON lines both ways (rig/PROTOCOL.md, section 2). The viewer, the CLI and the MCP server are its clients."""
from __future__ import annotations

import json
import os
import signal
import socket
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from . import commands  # noqa: F401  (registers the commands)
from .rig import Rig, Sub


def runtime_dir() -> Path:
    base = os.environ.get('XDG_RUNTIME_DIR') or f'/tmp/slrig-{os.getuid()}'
    d = Path(base) / 'slrig'
    d.mkdir(parents=True, exist_ok=True, mode=0o700)
    return d


def socket_path() -> Path:
    return Path(os.environ.get('SLRIG_SOCKET') or runtime_dir() / 'daemon.sock')


class Client(threading.Thread):
    def __init__(self, server: 'Server', conn: socket.socket, n: int):
        super().__init__(name=f'client-{n}', daemon=True)
        self.server, self.conn, self.rig = server, conn, server.rig
        self.sub = Sub(f'client-{n}')
        self.name = 'cli'
        self.alive = True
        self.writer = threading.Thread(target=self._write, name=f'client-{n}-w', daemon=True)

    def run(self):
        self.writer.start()
        f = self.conn.makefile('r', encoding='utf-8', errors='replace', newline='\n')
        try:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    req = json.loads(line)
                except ValueError:
                    self.reply({'id': None, 'ok': False, 'error': {'code': 'json', 'message': 'not JSON', 'hint': line[:80]}})
                    continue
                self.handle(req)
        except OSError:
            pass
        finally:
            self.alive = False
            self.rig.unsubscribe(self.sub)
            self.sub.put('', droppable=False)
            try:
                self.conn.close()
            except OSError:
                pass

    def reply(self, obj: dict) -> None:
        self.sub.put(json.dumps(obj, default=_json_default), droppable=False)

    def handle(self, req: dict) -> None:
        rid, op = req.get('id'), req.get('op')
        if op == 'hello':
            self.name = str(req.get('client', 'cli'))
            self.reply({'id': rid, 'ok': True, 'result': {'rig': 1, 'commands': self.rig.commands(), 'status': self.rig.status()}})
        elif op == 'subscribe':
            self.rig.subscribe(self.sub, list(req.get('streams', [])))
            self.reply({'id': rid, 'ok': True, 'result': sorted(self.sub.streams)})
        elif op == 'unsubscribe':
            self.rig.unsubscribe(self.sub, list(req.get('streams', [])))
            self.reply({'id': rid, 'ok': True, 'result': sorted(self.sub.streams)})
        elif op == 'run':
            cmd, args = req.get('cmd', ''), req.get('args') or {}
            def work():
                env = self.rig.run(cmd, args, self.name)
                env['id'] = rid
                self.reply(env)
            self.server.pool.submit(work)
        else:
            self.reply({'id': rid, 'ok': False, 'error': {'code': 'op', 'message': f'no op {op!r}', 'hint': 'hello, run, subscribe, unsubscribe'}})

    def _write(self):
        while True:
            line = self.sub.q.get()
            if not self.alive and line == '':
                return
            try:
                self.conn.sendall((line + '\n').encode())
            except OSError:
                self.alive = False
                return


def _json_default(o):
    import numpy as np
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    if isinstance(o, np.ndarray):
        return o.tolist()
    if isinstance(o, Path):
        return str(o)
    return str(o)


class Server:
    def __init__(self, path: Path | None = None, source: str = 'auto', scene: str = 'still'):
        self.path = path or socket_path()
        self.run_dir = self.path.parent
        self.rig = Rig(self.run_dir)
        self.rig.socket_path = self.path
        self.pool = ThreadPoolExecutor(max_workers=16, thread_name_prefix='cmd')
        self.source, self.scene = source, scene

    def serve(self) -> None:
        if self.path.exists():
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                s.connect(str(self.path))
                s.close()
                raise SystemExit(f'slrigd is already running ({self.path})')
            except (ConnectionRefusedError, FileNotFoundError):
                self.path.unlink(missing_ok=True)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        old = os.umask(0o177)
        srv.bind(str(self.path))
        os.umask(old)
        srv.listen(16)
        (self.run_dir / 'daemon.pid').write_text(str(os.getpid()))
        if self.source != 'none':
            env = self.rig.run('connect', {'source': self.source, 'scene': self.scene}, 'rig')
            if not env['ok']:
                self.rig.notice('start: ' + env['error']['message'], 'warn')

        def stop(*_):
            try:
                self.rig.disconnect()
            finally:
                self.path.unlink(missing_ok=True)
                os._exit(0)
        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        n = 0
        while True:
            conn, _ = srv.accept()
            n += 1
            Client(self, conn, n).start()


def main(source: str = 'auto', scene: str = 'still') -> None:
    Server(source=source, scene=scene).serve()
