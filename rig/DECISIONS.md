# Decisions

## D1. The machine (inspected 2026-10-27)

- **Computer:** Ryzen 7 3700X, RTX 3060 (driver 610.57), 32 GB, Arch / Omarchy, Hyprland (Wayland).
- **Audio:** PipeWire 1.6.8. The user is in `realtime`.
- **Toolchains:**
  - clang 22, gcc 16, cmake, ninja;
  - Python 3.14.7, uv 0.12.7, numpy 2.5.3;
  - glfw 3.5.1, glm 1.0.3, mesa 26.2.2;
  - kicad 10.0.6, blender 5.2.1;
  - ESP-IDF 5.5.1.
- **Missing pieces:**
  - there is no Xvfb with GLX (unablenot's own Xvfb is built without GL);
  - stb and NanoVG are not installed, but are vendored elsewhere (swarm, signalpatch).
- **Spikes:**
  - EGL with `EGL_PLATFORM_SURFACELESS_MESA` gives a GL 3.3 core context on the RTX 3060 with no display
    server;
  - `kicad-cli pcb export vrml` exports both rev C boards with their parts (3.4 MB and 7.3 MB).

## D2. The viewer stack

Scored 1 to 5 against what the rig needs:

| | C++ GLFW + GL 3.3 + NanoVG | Python PySide6 + moderngl | Tauri / web |
|---|---|---|---|
| low lag from motion to screen (judging the gyro by eye) | 5 | 3 | 3 |
| smooth with 1 kHz plots and a 3D model | 5 | 3 | 4 |
| offscreen shots for Claude | 5 (EGL, spiked) | 4 | 3 |
| panels, sliders, forms | 3 | 5 | 5 |
| fits the user's stack | 5 (instrument UIs) | 4 (Carapace) | 2 (user prefers no web UI) |
| speed for one agent overnight | 3 | 4 | 4 |

Chosen: **C++ GLFW + GL 3.3 + NanoVG**. Seeing tilt lag is the point of the tool, so the viewer itself
must add little. The panels cost more to draw by hand, and that is accepted.

## D3. A daemon as the hub, in Python

One process owns the MIDI port. The viewer is a client and can be closed, so Claude can still work
headless through MCP. numpy does the analysis. This follows the Carapace pattern (registry, one `run`,
MCP over stdio, AGENTS.md generated).

## D4. Everything over MIDI SysEx

The board has no free USB endpoint for a CDC port. The motion stream reuses the core's `m` format byte
for byte, so the existing tools read Samplooler takes. The new letters (`Q`, `O`, `T`, `J`, `K` and their
lowercase answers) do not collide with `R`, `S`, `L`, `M`, `I`, `D`, `P`, `V`.

## D5. The twin runs the firmware's own C

The rig never models the firmware in Python. Replays and A/B comparisons run the real `motion.c` and `gesture.c`.

## D6. Today's feel stays the default

Preset `v1` is today's behaviour. `twin/test_v1.c` checks it over 4.8 million comparisons with copies of the old
code: the angles are equal to the bit, and every decision (zone, tap, hold, long, flick, count) is the same. The
speeds and effect amounts are within 3e-6, because with `-ffast-math` a division by a `#define` becomes a multiply
by its reciprocal, which a division by a tuning value does not. New fusion and mapping are opt-in tuning values until we
have measured them on a board with the user's hands. When something sounds wrong and the numbers look
fine, A/B `v1` against the new preset.

## D7. Where the work lives

Branch `rig`, in the worktree `~/Projects/samplooler/.rig`, from `air-easy` (86e19db), which has the core's
motion recorder. The main tree's uncommitted work (SIFT, `boards.py`, `pins.h`, …) is left alone. Nothing
is pushed. Sessions (the user's motion and audio) go to the private `lab/rig/sessions/`.

## D8. Screenshots without windows

The viewer's `--shot` renders into an FBO through EGL surfaceless, so it needs no X or Wayland. Nothing
opens on the user's screen while testing.

## D9. The 3D model

The model is the rev C Stack (tag `checkpoint/boards-rev-c`), the boards the user has. It is exported
from KiCad as VRML at build time and parsed the way `lab/tools/showcase/vrml.py` does. The screen is found
by its material colour (0.05, 0.25, 0.45). The boards are 94 x 46 mm, 11 mm apart.

## D10. No flashing from the rig

Flashing stays with `tools/flash.sh`, only when the user asks, and never on the TWOFOLD.

## D11. Every input, every gesture (the user's addition)

One table (`tune.h`, 68 values) covers the IMU, motion, tilt, roll, joystick, encoder, REC, mic, USB and the rig.
`gesture.c` holds every decision and reports each gesture as an event.

## D12. The motion stream is the rig's own (`r`), not the core's `m`

The core's `m` carries 1/1024 g and 1/4 deg/s, which is coarser than the gyro's noise (about 0.03 deg/s). It could
not show a bias. `r` sends the chip's raw 16 bits. Takes on disk still use motion_rec's units.

## D13. The twin is also the calculator

`twin --calc` computes the curve and replays raw takes with any tuning, through the same C as the board. Python
never re-implements the firmware.

## D14. Injection replaces the sensor, paced by the rig

While 'J' samples arrive, the board's FIFO samples are dropped, until 0.5 s after the last one. The daemon sends
them at their own rate, and the twin takes them at the FIFO's pace.

## Log

- 2026-10-27. The Samplooler's control code runs at 50 Hz (`tick % 2`) inside a 10 ms loop.
- 2026-10-27. The Samplooler's SysEx receive buffer is 48 bytes (`usb.c sx[48]`). Messages sent to the
  board stay short, or the buffer grows.
- 2026-10-27. The MIDI TX buffer is 1024 bytes (`CFG_TUD_MIDI_TX_BUFSIZE`). A 1171-byte screen message
  does not fit.
- 2026-10-27. The unix socket path has a 108-character limit: the scratchpad was too long. Test daemons live
  under `/run/user/1000/slrig-test`.
- 2026-10-27. `tools/flash.sh` takes the first `/dev/serial/by-id/*Espressif*`. The TWOFOLD is an Espressif
  device: always give `PORT=` explicitly.
- 2026-10-27. The Samplooler's `sl_midi.py` finds boards named Samplooler only. A board running HUFF needs the
  core's `sl_midi.py` (family) to reboot into download mode.
