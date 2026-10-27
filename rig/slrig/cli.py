"""slrig: the rig from a terminal.

  slrig serve [--source auto|board|twin|none] [--scene still|demo]   the daemon, in the foreground
  slrig start | stop                                                 the daemon in the background / stop it
  slrig run CMD [name=value ...]                                     any command (slrig help lists them)
  slrig help [CMD|GROUP]                                             the commands, or one command's arguments
  slrig status | tune | gestures | events | log                      the usual looks
  slrig watch                                                        the state, live, one line
  slrig view [--shot out.png]                                        the viewer (starts the daemon if needed)
  slrig mcp                                                          the MCP server on stdio (for Claude)
  slrig agents-md [--out FILE]                                       the command reference from the registry
"""
from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

RIG = Path(__file__).resolve().parent.parent


def _val(s: str):
    try:
        return json.loads(s)
    except ValueError:
        return s


def _print(env: dict) -> int:
    if env.get('ok'):
        r = env.get('result')
        print(json.dumps(r, indent=1, default=str) if not isinstance(r, str) else r)
        return 0
    e = env.get('error', {})
    print(f"error {e.get('code')}: {e.get('message')}" + (f"\n  {e.get('hint')}" if e.get('hint') else ''), file=sys.stderr)
    return 1


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    cmd, rest = argv[0], argv[1:]
    if cmd == 'serve':
        ap = argparse.ArgumentParser(prog='slrig serve')
        ap.add_argument('--source', default='auto', choices=['auto', 'board', 'twin', 'none'])
        ap.add_argument('--scene', default='still', choices=['still', 'demo'])
        a = ap.parse_args(rest)
        from .daemon import main as serve
        serve(a.source, a.scene)
        return 0
    if cmd == 'mcp':
        from .mcp_server import main as mcp_main
        mcp_main()
        return 0
    if cmd == 'agents-md':
        from . import commands  # noqa: F401
        from .registry import agents_md
        text = agents_md()
        if '--out' in rest:
            Path(rest[rest.index('--out') + 1]).write_text(text + '\n')
        else:
            print(text)
        return 0
    if cmd == 'stop':
        from .daemon import socket_path
        pid_file = socket_path().parent / 'daemon.pid'     # beside the socket in use (SLRIG_SOCKET moves both)
        if not pid_file.exists():
            print('slrigd is not running')
            return 0
        try:
            os.kill(int(pid_file.read_text()), signal.SIGTERM)
            print('slrigd stopped')
        except (ProcessLookupError, ValueError):
            print('slrigd was not running')
        pid_file.unlink(missing_ok=True)
        return 0
    if cmd == 'view':
        from .client import Client
        Client('cli').close()                     # the daemon first
        exe = RIG / 'view' / 'build' / 'slrig-view'
        if not exe.exists():
            print('the viewer is not built: cmake -S rig/view -B rig/view/build -G Ninja && cmake --build rig/view/build', file=sys.stderr)
            return 1
        if '--shot' in rest:
            return subprocess.call([str(exe)] + rest)
        subprocess.Popen([str(exe)] + rest, start_new_session=True, stdin=subprocess.DEVNULL)
        return 0

    from .client import Client
    c = Client('cli')
    if cmd == 'start':
        return _print(c.run('status'))
    if cmd == 'run':
        if not rest:
            print('slrig run CMD [name=value ...]', file=sys.stderr)
            return 2
        args = dict(kv.split('=', 1) for kv in rest[1:] if '=' in kv)
        return _print(c.run(rest[0], {k: _val(v) for k, v in args.items()}))
    if cmd == 'help':
        return _print(c.run('help', {'topic': rest[0]} if rest else {}))
    if cmd == 'status':
        return _print(c.run('status'))
    if cmd == 'tune':
        return _print(c.run('tune.list', {'group': rest[0]} if rest else {}))
    if cmd == 'gestures':
        return _print(c.run('gestures'))
    if cmd == 'events':
        return _print(c.run('events.tail', {'n': 40}))
    if cmd == 'log':
        return _print(c.run('log.tail', {'n': 40}))
    if cmd == 'watch':
        c.request('subscribe', streams=['state', 'event'])
        while True:
            line = c.f.readline()
            if not line:
                return 0
            m = json.loads(line)
            if m.get('ev') == 'state':
                print(f"\r{m['state']:4} {m['mode']:5} pitch {m.get('pitch', 0):+6.1f} roll {m.get('roll', 0):+6.1f} "
                      f"speed {m.get('speed_cmd', 0):+5.2f} ({m.get('speed_now', 0):+5.2f}) {m.get('zone', ''):6} "
                      f"delay {m.get('delay', 0):.2f} stutter {m.get('stutter', 0):.2f}{' FROZEN' if m.get('frozen') else ''}   ",
                      end='', flush=True)
            elif m.get('ev') == 'event':
                print(f"\n  {m['t']:9.2f}  {m['name']} {m.get('text', '')} {m.get('ms') or ''}")
    # a bare command name: slrig tune.set name=tilt.dead value=6
    args = dict(kv.split('=', 1) for kv in rest if '=' in kv)
    return _print(c.run(cmd, {k: _val(v) for k, v in args.items()}))


if __name__ == '__main__':
    sys.exit(main())
