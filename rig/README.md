# Samplooler Rig

A bench on the computer for a Samplooler on USB. It shows everything the board senses and decides, lets you
change every number behind the controls while it plays, records and replays gestures, and measures the result.
Claude works on the same bench through MCP.

Status: built and tested against the twin (the firmware's own code running on the PC): 190 tests pass. Not yet run on a
board. `MORNING.md` is the first session with one.

## What is in it

| part | what |
|---|---|
| firmware (`../firmware/main`) | reads the gyro (it only read the accelerometer before); `tune.c` holds every number behind the controls, changeable live and kept in NVS; `motion.c` (ACC, COMP, MAHONY), `gesture.c` (every control decision, each one an event), `rig.c` (the streams) |
| `twin/` | the firmware's own C built on the PC, behind the same SysEx: the rig runs without a board |
| `slrig/` | the daemon (`slrigd`), its commands, the CLI, OSC, the MCP server, analysis, guided tests |
| `view/` | the viewer: the board in 3D turned by its IMU, its screen, plots, the tuning panel |

The defaults of the tuning table are the firmware as it was (preset `v1`). `twin/test_v1.c` checks that against
copies of the old code: the angles come out equal to the bit, every decision (zone, tap, hold, flick) the same.

## Install

```sh
rig/tools/install.sh        # uv sync, builds the twin and the viewer, ~/.local/bin/slrig and slrig-view,
                            # a menu entry, and the MCP server "slrig" for Claude Code
```

## Use

```sh
slrig-view                  # the viewer; the daemon starts with it (a board if one is plugged in, else the twin)
slrig status                # where things stand
slrig watch                 # the state on one line, the gestures as they fire
slrig run tune.set name=motion.fusion value=COMP
slrig run test.run name=still
slrig help                  # every command; slrig help tune.set for one
```

The firmware with the rig in it must be on the board (`tools/flash.sh`, only when you choose to).

## Ports

- MIDI: the daemon owns the board's raw MIDI port while it runs. Close the desktop app and the plugin first, or
  the port is busy.
- OSC out to 127.0.0.1:9000: `/samplooler/motion`, `/angles`, `/quat`, `/speed`, `/fx`, `/state`, `/event`.
  OSC in on port 9001: `/samplooler/tune/<name>`, `/setting/<i>`, `/rec`, `/enc`, `/switch`, `/run`.
  `slrig/osc.py` has the full list.
- The daemon's socket: `$XDG_RUNTIME_DIR/slrig/daemon.sock`. Its log is `daemon.log` beside it.

## Files

- `PROTOCOL.md`: the SysEx, the socket, the session folder.
- `PLAN.md`, `DECISIONS.md`, `PROGRESS.md`, `ROADMAP.md`: how it was built and what is next.
- `AGENTS.md`: every command, generated from the registry (`slrig agents-md --out rig/AGENTS.md`).
- Sessions go to `lab/rig/sessions/` (private), or to `$SLRIG_SESSIONS`.
