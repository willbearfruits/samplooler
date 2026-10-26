"""The MCP server (stdio): Claude on the same bench as the user. It is a client of slrigd, so whatever Claude does shows
in the viewer's log strip, and whatever the user does is in rig_state. Few, general tools, as Carapace's: rig_help
discovers the commands, rig_run runs any of them, rig_batch runs several, and three tools return pictures."""
from __future__ import annotations

import json
import threading
from typing import Any

INSTRUCTIONS = """The Samplooler Rig: a bench on the PC for a Samplooler (an ESP32-S3 tilt looper) on USB, or for its twin
(the firmware's own code on the PC) when no board is plugged in. Everything the board senses (accelerometer, gyro,
encoder, REC, joystick, mic) and decides (angles, speed, delay, stutter, gestures) streams in; every number behind the
controls is a tuning value that changes live (tune.set) and is kept with tune.save.

Work loop: rig_state() to see where it stands -> rig_help() for the commands, rig_help("tune.set") for one ->
rig_run(command, args). Pictures: rig_screen() is the board's OLED, rig_view() the viewer (3D board, plots, panels),
rig_plot(session) a recorded session. Results are {"ok": true, "result": ...} or {"ok": false, "error": {...}}.

Tuning a gesture: record it (test.run name=steps, or record.start / record.stop while the user plays), then
session.analyze, then session.compare a=v1 b=gyro (the same recorded hand through two tunings), then tune.set and
ask the user to play again. Axes: device x to the screen's right, y to its bottom edge, z out of the screen; pitch +
with the nose (top edge) down, roll + with the right edge down. Never flash a board: the rig cannot, and only the user
says when."""


def build_server():
    from mcp.server.mcpserver import Image, MCPServer
    from .client import Client

    server = MCPServer(name='slrig', instructions=INSTRUCTIONS, version='0.1.0')
    lock = threading.Lock()
    holder: dict[str, Any] = {'c': None}

    def call(cmd: str, args: dict | None = None) -> dict:
        with lock:
            for attempt in (0, 1):
                try:
                    if holder['c'] is None:
                        holder['c'] = Client('mcp')
                    return holder['c'].run(cmd, args or {})
                except (OSError, ConnectionError):
                    holder['c'] = None
                    if attempt:
                        raise
        return {'ok': False, 'error': {'code': 'daemon', 'message': 'no daemon'}}

    @server.tool()
    def rig_help(topic: str = '') -> str:
        """The rig's commands. No topic: all, by group, one line each. A command's name: its arguments (JSON Schema)
        and examples. A group (tune, test, session, inject, ...): its commands."""
        return json.dumps(call('help', {'topic': topic}), indent=1)

    @server.tool()
    def rig_run(command: str, args: dict | None = None) -> str:
        """Run one rig command, e.g. rig_run("tune.set", {"name": "tilt.dead", "value": 6}),
        rig_run("test.run", {"name": "still"}), rig_run("session.compare", {"a": "v1", "b": "gyro"})."""
        return json.dumps(call(command, args or {}), indent=1, default=str)

    @server.tool()
    def rig_batch(steps: list[dict], stop_on_error: bool = True) -> str:
        """Run several commands in order: [{"command": "...", "args": {...}}, ...]. Stops at the first error unless
        stop_on_error is false."""
        out = []
        for st in steps:
            env = call(st.get('command', ''), st.get('args') or {})
            out.append({'command': st.get('command'), **env})
            if stop_on_error and not env.get('ok'):
                break
        return json.dumps(out, indent=1, default=str)

    @server.tool()
    def rig_state() -> str:
        """Where things stand: source (board/twin/none), the board, stream rates and drops, recording, the latest angles,
        speed and effects, the tuning values that differ from v1, and the last gestures and log lines."""
        st = call('status')
        changed = call('tune.list', {'changed': True})
        ev = call('events.tail', {'n': 12})
        lg = call('log.tail', {'n': 8})
        return json.dumps({'status': st.get('result'), 'tune_changed': changed.get('result'),
                           'events': ev.get('result'), 'log': lg.get('result')}, indent=1, default=str)

    @server.tool()
    def rig_screen(scale: int = 4) -> list:
        """The board's OLED screen right now, as a picture."""
        env = call('screen.png', {'scale': scale})
        if not env.get('ok'):
            return [json.dumps(env)]
        return [Image(path=env['result']['png']), json.dumps(env['result'])]

    @server.tool()
    def rig_view(tab: str = '', width: int = 1600, height: int = 1000) -> list:
        """The viewer, rendered offscreen: the 3D board turned by the live IMU, the OLED, the plots and a panel
        (tab: tune, gestures, settings or session)."""
        env = call('view.shot', {'tab': tab, 'width': width, 'height': height})
        if not env.get('ok'):
            return [json.dumps(env)]
        return [Image(path=env['result']['png']), json.dumps(env['result'])]

    @server.tool()
    def rig_plot(session: str = 'last', fields: str = 'acc,gyro,angles,speed', t0: float = 0, t1: float = 0) -> list:
        """A recorded session drawn: stacked panels (acc, gyro, angles, speed, fx, joy, levels), gestures along the
        bottom, a guided test's steps shaded."""
        env = call('session.plot', {'name': session, 'fields': fields, 't0': t0, 't1': t1})
        if not env.get('ok'):
            return [json.dumps(env)]
        return [Image(path=env['result']['png']), json.dumps(env['result'])]

    return server


def main() -> None:
    build_server().run('stdio')
