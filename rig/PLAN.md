# Samplooler Rig: the plan

A bench on the PC for a Samplooler on USB. It shows everything the board senses and decides, changes any
of it live, records and replays gestures, and measures the result in numbers and in sound. Claude drives
the same bench through MCP. The first job is the gyroscope.

Status: milestones 0 to 8 done 2026-10-27 02:20 (PROGRESS.md); 9 waits for a board. Branch `rig` in the worktree `~/Projects/samplooler/.rig`,
from `air-easy` 86e19db. Not pushed.

> **USER (2026-10-27):** "i do want tuning of everything, the gyro, the accelerometer, the mic, the
> joystick, the encoder, we go into leveling up the device and i need to understand and see and tune all
> the gestures"

So the rig covers every input, not only tilt:

- One tuning table holds every input's numbers.
- Every input streams raw.
- Every gesture is an event you can see as it fires, with its timing and the thresholds that decided it.

## Everything that gets tuned (firmware `tune.c`, one table, saved in NVS `tune`)

Each value has a name, group, range, step, unit, default (= today's behaviour) and a one-line help.

| group | values (today's value) |
|---|---|
| `imu` | accel range (±2 g), gyro range (±250 °/s), DLPF (44 Hz), accel offset x/y/z (0), gyro bias x/y/z (0), auto bias at rest (off), rest threshold |
| `motion` | fusion ACC / COMP / MAHONY (ACC), accel smoothing (0.25), COMP time constant, Mahony kp/ki, control rate 50/100 Hz (50), axis map and signs (pitch from -x, roll from y, TILT_SIGN -1) |
| `tilt` | dead (5°), full (45°), curve (today's), detents at 1x/-1x/2x (off), detent width, hysteresis (0), speed min/max stay settings |
| `roll` | sign (+1), dead (15°), full (45°) for delay (left) and stutter (right) |
| `joy` | on/off (off: HAS_JOY 0), dead zone (0.12), smoothing (0.3), rotation (a quarter turn), flick at (0.7), re-arm at (0.3), centre (measured at boot) |
| `enc` | direction (-1), counts per detent (4), glitch filter (1 µs), tap min (40 ms), tap max = hold (500 ms), long press (3 s), acceleration (off) |
| `rec` | debounce, factory-reset hold (3 s), stuck (6 s) |
| `mic` | high-pass corner (80 Hz), slot L/R/auto (auto), trim, a gate (off) |
| `usb` | what the board sends to the PC: the mix on both channels (today), or the mix left and the raw mic right (the rig measures both at once) |

Gestures the rig shows as events, with counters:

- tilt zones entered, a detent caught, freeze on and off;
- roll into the delay or the stutter;
- encoder turn, tap, hold and long press;
- REC down and up (with ms), the factory-reset and stuck timers;
- joystick flicks;
- menu open and close, record start and stop.

The firmware code is split so the PC runs it too:

- `imu.c` and `input.c` only read the hardware;
- `motion.c` turns IMU samples into orientation;
- `gesture.c` makes every decision about the controls;
- `rig.c` handles the rig's streams.

## What is there today

- The Samplooler reads only the accelerometer (MPU-6050 at ±2 g). Pitch and roll come from `atan2`,
  smoothed by a one-pole of 0.25 at 50 Hz. The gyro is set up (±250 °/s) and never read.
- Tilt to speed lives in `main.c`: `control_to_speed()` (dead 5°, full 45°, a curve) and the roll mapping
  (dead 15°, full 45°) for delay and stutter. All of it is compile-time constants.
- The board talks over USB MIDI only (all four IN endpoints are used, so there is no CDC):
  - logs come out as SysEx `L`, and `S` asks for the status;
  - `I`/`D`/`P`/`V` handle settings;
  - CC 1 to 7 carry 7-bit telemetry, too coarse for tuning.
- The core (the instruments) already streams the IMU at 1 kHz:
  - SysEx `M` starts the stream, which arrives as `m` messages: accel and gyro, 14-bit;
  - `motion_rec.py` records takes and `sim/take.c` replays them;
  - this ran on boards with AIRDWELLER.
- `uisim` and the showcase twin compile `ui.c`, `oled.c`, `recorder_f32.c` and `fx.c` on the PC.
- No Samplooler board is plugged in tonight. Only the TWOFOLD is, and it is never touched.

## How it fits together

```
  board (USB MIDI + UAC2)        twin (the firmware's own C, built on the PC)
            \                       /
             slrigd   owns the port, decodes the streams, records sessions, analyses,
             |        OSC in and out, PipeWire capture and playback, the command list
             |   unix socket: JSON lines, plus binary stream frames
     --------+-----------------------------
     |               |                    |
  slrig-view      slrig (CLI)       slrig mcp (stdio, for Claude)
  3D stack, OLED, plots, tuning panel, prompts; --shot renders offscreen through EGL
```

Every action is a named command, in the Carapace pattern. The viewer, the CLI and the MCP call the same
commands on the same daemon, so the user and Claude always see the same state. The viewer's log strip
shows Claude's actions as they happen.

## Firmware (`firmware/main/`)

1. **IMU.** Copy in the core's `imu.c` unchanged: accel and gyro, with the FIFO's 1 kHz burst. Reading
   ranges and the filter become tuning values.
2. **`tilt.c`.** New, pure C, also built on the PC. Everything that turns motion into speed and effects
   moves here from `main.c`:
   - Fusion has three modes:
     - ACC: today's, exactly;
     - COMP: a complementary filter, gyro plus accel;
     - MAHONY.
   - Gyro bias is learned while the board is at rest.
   - Axis map and signs.
   - Dead zone, full angle, curve, and detents (sticky 1x, -1x, 2x).
   - The roll mapping, freeze, and the 50 or 100 Hz rate.
   - **Preset `v1` reproduces today's behaviour bit for bit**, checked on the PC against the old code.
     It stays the default until we have measured on a board together.
3. **The rig's SysEx.** These are new letters. Nothing existing changes, so the plugin and the app keep
   working.

   | sent | answer | what |
   |---|---|---|
   | `M` 1/0 | `m` | motion, 1 kHz: the core's exact format, so `motion_rec.py` and `take.c` read Samplooler takes |
   | `Q` 1/0 | `q` | state, 50 Hz: quaternion, pitch/roll/yaw, speed asked and actual, position, REC, fx amounts, CPU, levels, I2C failures |
   | `O` 1/0 | `o` | the OLED's 1024 bytes when the screen changes, at most 20 fps |
   | `T` | `t` | the tuning table: list, get, set, save, reset, preset |
   | `J` | | injected motion replaces the sensor; the sensor is back after 0.5 s with no injection |
   | `K` | | keys: encoder turn, tap and hold; REC down and up (drive the menus from the PC) |

   Bandwidth: motion is 12 KB/s and already runs on boards; the screen is at most 23 KB/s; the state is
   2 KB/s. The MIDI TX buffer grows from 1 KB to 4 KB. When it fills, the screen drops frames first.
   Sequence numbers show every drop.
4. **Tuning in its own NVS blob (`tune`).** The user's settings are untouched. Values are clamped on load,
   and safe mode falls back to `v1`.
5. **Checks:**
   - it builds with `-Werror`;
   - the `uisim` sheet is unchanged;
   - `flocksim` and the soak pass;
   - nothing new on the audio task, whose CPU is measured on the board.

## Twin (`rig/twin/`)

A PC program built from the firmware's `tilt.c`, `recorder_f32.c`, `fx.c`, `ui.c`, `oled.c`, the fonts
and the rig's SysEx encoder:

- it speaks the same SysEx on stdin and stdout;
- motion comes from a take, a scripted gesture, or injection;
- so every part of the rig works with no board, and the tests run against it.

Limit: its loop is a small stand-in for `main.c`. Menus, slots, voice and the flock are not in it.

## Daemon, CLI, MCP (`rig/slrig/`, Python 3.14 + uv; numpy, `mcp`)

- **Port:** found by name as `boards.py` does. Never the TWOFOLD, by name or by 303a:4a57. Raw ALSA MIDI
  with a 64 KB buffer.
- **Streams:**
  - decoders for `m`, `q`, `o`, `t`, `L`, `I`, `D`, `V`, feeding ring buffers;
  - a host reference fusion (Mahony at 1 kHz on the raw stream) to compare with the board's own.
- **Commands:** a registry like Carapace's (decorator, typed arguments, docstring help, one `run()` that
  returns `{ok, result | error}`), with AGENTS.md generated from it.
- **Sessions:** a recording goes to `lab/rig/sessions/<date>-<name>/`:
  - `motion.take`, in motion_rec's format;
  - `state.csv`;
  - the screen frames;
  - `audio.wav`;
  - `meta.json`, with the tuning and settings at the time.
- **Analysis:**
  - rest bias and noise per axis, and drift;
  - step response and lag, jitter of the speed at rest, overshoot;
  - saturation, an axis and sign check, I2C failures.
- **Replay:** a recorded gesture runs through `tilt.c` in the twin with other tuning values, and A/B tables
  compare the results. This is how we choose values from real hands instead of guessing.
- **Guided tests:** prompts on the viewer with a countdown:
  - still;
  - tilt-and-hold steps at 0, ±15, ±30 and ±45°;
  - a slow sweep;
  - flicks;
  - shake;
  - free play.
- **Audio:**
  - `pw-record` of the board's USB output: level, clicks, NaN and dead zeros, and the pitch of a known tone
    gives the real speed;
  - `pw-play` sends test tones into the board's USB input;
  - latency is measured from an injected step to the change in the sound.
- **OSC** (UDP, stdlib, ports in the config):
  - out: `/samplooler/motion`, `/angles`, `/speed`, `/state`, …;
  - in: `/samplooler/set/<setting>`, `/tune/<name>`, `/rec`, ….
- **MCP** (stdio, the `mcp` package, as `cara mcp` does):
  - tools: `rig_help`, `rig_run`, `rig_batch`, `rig_state`, `rig_screen` (the OLED as a PNG), `rig_view`
    (the 3D view as a PNG, offscreen) and `rig_plot`;
  - registered with `claude mcp add --scope user slrig`;
  - plus a small `samplooler-rig` skill for the tuning workflow.
- **No flashing from the rig.** Flashing stays with `tools/flash.sh`, and only when asked.

## Viewer (`rig/view/`, C++20, GLFW 3.5 + OpenGL 3.3 + NanoVG + glad, CMake + Ninja)

- **The rev C Stack in 3D:**
  - built from KiCad's VRML at build time into a small mesh file;
  - turned live by the board's quaternion, with the host reference as a faint ghost;
  - the OLED plays as a live texture on the model;
  - gravity and axis arrows.
- **The OLED, large and pixel exact.**
- **Scrolling plots:** accel, gyro, angles, speed asked against actual, output level.
- **The tilt → speed curve,** with the live point, the dead zone and the detents.
- **Tuning panel:**
  - every tuning value, set with a slider or the keys;
  - A/B between two tuning sets;
  - save to the board.
- **The board's own settings** (`D`/`P`/`V`), and the board status: firmware, serial, CPU, heap, drops,
  stream rates.
- **Record and sessions:** a record button, the session list, and the guided-test prompts.
- **`--shot out.png`** renders offscreen through EGL. Spike: a GL 3.3 core context on the RTX 3060 with no
  display server. No window opens on the user's screen during testing.
- **Frame time:** the target is 2 to 4 ms. Motion-to-photon (stream timestamp to frame) is measured and
  shown.

## Milestones

- [x] 0. Skeleton: uv project, CMake, the command registry, the test harness, the state files.
- [x] 1. Firmware: `tilt.c` and the gyro read. `v1` is identical to today's behaviour (PC check), and the
      firmware builds.
- [x] 2. Firmware: the rig's SysEx and the tuning NVS. The encoders are tested on the PC. `uisim`,
      `flocksim` and the soak pass.
- [x] 3. Twin speaks the protocol; scripted gestures; takes replay.
- [x] 4. Daemon: connect (board or twin), decode, registry, CLI, sessions, OSC.
- [x] 5. Analysis, replay A/B, guided tests, audio capture and measurement.
- [x] 6. MCP, AGENTS.md, the `samplooler-rig` skill, registration.
- [x] 7. Viewer: 3D, OLED, plots, panels, `--shot`.
- [x] 8. End to end on the twin; screenshots to the phone; `rig/README.md`; launcher and menu entry; a
      morning checklist.
- [ ] 9. Morning, with the user: plug in a board. With the user's OK, flash the rig firmware. Then the
      static checks (bias, noise, rates, CPU), then the user moves it and we tune.

## How each part is checked

- **Firmware:** the `idf.py build` with `-Werror`, `uisim`, `flocksim` and the soak.
- **`tilt.c`:** host tests.
  - `v1` against the old code on a sweep of inputs, which must be exact.
  - Fusion converges.
  - The bias is learned.
  - Garbage in gives no NaN.
- **Protocol:** round trips between the firmware's encoders, built on the PC, and the Python decoders.
- **Analysis:** synthetic signals with known answers (a known bias, noise, lag, steps).
- **End to end:** daemon, twin and viewer offscreen. The MCP lists its tools and runs a guided test
  against the twin.
- **Viewer:** `--shot` images are checked for content, and frame time is reported.
- **Screenshots** go to `rig/docs/screenshots/` and to the user's phone as milestones land.
