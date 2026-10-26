# Progress

## 2026-10-27 01:45

**Done and checked:**

- **Firmware** (`firmware/main`):
  - `tune.c`, `motion.c`, `gesture.c`, `rig.c`, the gyro through the FIFO, `input.c` on `gesture.c`;
  - the mic's high-pass, slot and trim, and USB stereo (the mix, plus the raw input);
  - built with `-Werror`: 800 KB, DIRAM 197.9 of 341.8 KB static.
- **v1 check** (`twin/build.sh`): 4.8 M comparisons, 0 differences.
- **Twin:** streams every message. `--calc` gives the curve and replays.
- **Daemon** (`slrig`): 45 commands, OSC, sessions, MCP (7 tools, images).
  - `tests/test_wire.py` and `tests/test_daemon.py`: 14 pass in 7 s.
  - On the twin: motion at 502 and 1003 Hz, state 50 Hz, no drops.
  - The keys fire the firmware's gestures, and a recorded session replays through other tunings.

**Running:**

- The viewer (C++, a helper agent).
- Analysis, guided tests, synthetic gestures and plots (a helper agent).

**Not possible tonight:**

- Anything on a board. Only the TWOFOLD is plugged in.
- Heap and CPU with the rig's streams, USB throughput and the axis map are to be measured in the morning
  (`MORNING.md`).

**Next:**

1. Integrate both agents' work.
2. End to end with the viewer offscreen, and screenshots.
3. `AGENTS.md`, the install, the MCP registration, a skill for the workflow.
