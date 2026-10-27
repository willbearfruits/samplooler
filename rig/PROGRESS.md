# Progress

## 2026-10-27 02:20: the rig is built, tested on the twin, installed

Everything runs with no board: the twin is the firmware's own code on the PC. Nothing has run on a board yet,
because only the TWOFOLD was plugged in.

### Done and checked

**Firmware** (`firmware/main`, branch `rig`). It builds with `-Werror`: 800 KB, DIRAM 197.9 of 341.8 KB static.

- **Reading the sensors:**
  - The gyro is read through the FIFO at 100 to 1000 Hz. Before, the firmware read the accelerometer only.
  - `input.c` reads the pins. The decisions moved to `gesture.c`.
  - The mic: high-pass, slot and trim. USB can send the raw input on the right channel.
- **The tuning table** (`tune.c`): 68 live values, kept in NVS (`tune`). The defaults are v1.
- **Motion and gestures:**
  - `motion.c`: ACC (v1), COMP and MAHONY, gyro bias learned at rest.
  - `gesture.c`: every decision, each one reported as an event.
- **The rig's line** (`rig.c`):
  - streams out: raw motion, state, screen and events;
  - in: the tuning table both ways, injected motion and keys.
  - The state also carries the UI loop's time and the rig's dropped messages, for the first board run.

**Checks on the firmware:**

- v1 check: 4.8 M comparisons with copies of the old code, 0 differences (the angles equal to the bit).
- `tools/uisim`: every screen pixel-identical to the base commit.
- `tools/flocksim`: 31/31.
- `tools/soak` 0.25 h: 0 failures.

**Twin** (`rig/twin`): the firmware's C on the PC, behind the same SysEx. `--calc` gives the curve and A/B
replays.

**Daemon, CLI, OSC, MCP** (`rig/slrig`):

- 46 commands (`AGENTS.md`). Sessions, guided tests, analysis, replays, plots, audio.
- OSC both ways, checked by listening on 9000 and sending to 9001.
- MCP: 7 tools, images included, connected in Claude Code.

**Viewer** (`rig/view`):

- The rev C Stack in 3D turned by the IMU, the OLED on its glass and large, curves, six plots.
- Panels: tune, gestures, settings, session.
- Frame time 2.0 ms CPU, 2.7 ms CPU+GPU (1600x1000).
- The live window was tested on a private X server: it opens, draws and swaps.

**Tests:** 190 pass in 13 s (`cd rig && uv run pytest -q`).

**Installed:**

- `~/.local/bin/slrig` and `slrig-view`, and a menu entry ("Samplooler Rig").
- The MCP server `slrig` (user scope).
- The skill `~/.claude/skills/samplooler-rig`.

### What the twin says about the gyro

These come from the firmware's own code, with synthetic hands and the twin's sensor model (noise 0.004 g and
0.06 °/s, bias 0.9 / -0.6 / 0.4 °/s). A real board and real hands come next.

| what | v1 (accelerometer only) | gyro (COMP) |
|---|---|---|
| a 0.5 g shake without tilting: pitch swing | 21.8° | 2.2° |
| the same shake: speed flutter | 369 cents (the tape flips direction) | 0 |
| lag behind a move | 62 ms | |
| flicks of 30°: peak reached, delay | 55 %, 33 ms | |
| board vs gravity in held steps | within 0.14° | |

- The still test recovers a known gyro bias and accelerometer offset exactly. The board's own bias learner reaches
  97 % in 10 s.
- The guided tests all run end to end, acted out by the rig: still, axes, steps, sweep, flicks, shake, roll,
  encoder, rec.

### Not done, or not possible tonight

- **Anything on a board.** These need measuring:
  - heap with the rig's streams (`heap_kb`);
  - the UI loop's time (`loop_ms`) with the FIFO at 500 Hz and the OLED;
  - USB throughput (`wire_drops`);
  - the sign of the z axis (`axes` test).
- **The viewer has no text entry** (sliders, keys and buttons only).
- **The core instruments** (HUFF, AIRDWELLER...) do not speak the rig's line. ROADMAP.

### Next

`MORNING.md`.

## 2026-10-27 13:00: first board, 7275FBD4

- Flashed the rig firmware (esptool on the board's own by-id port; `tools/flash.sh` was not used, because it can
  send its reboot message to a TWOFOLD).
- Measured: CPU 9-11 %, internal heap 41 KB free, the UI loop 3-5 ms, no wire drops, no I2C failures. The state
  stream arrives at 24-35 Hz instead of 50: the loop has time to spare, so the cause is in how the state is sent.
- The axes: on this board y and z were reversed (a 180° turn about x). Flat and screen up, it read z = -1.12 g; with
  the nose (the USB-C edge, the top of the screen) down, it read nose up. So with the v1 map the nose down played
  backward, against the manual. The user wants nose down forward. Saved on this board only: `motion.ax_y=+x`,
  `motion.ax_z=+z` (everything else back to the defaults). 727600D0 must be checked the same way before the
  firmware's defaults change: the IMU module may be soldered on either way round.
- |a| reads 1.12 g flat: the accelerometer's z needs an offset or a scale (later, with the still test).
- At rest the gyro's spread was 0.7-2.3 deg/s with a loop playing: far above the twin's 0.06. To check with the
  loop stopped (the speaker may shake the board).
- The tuning save wrote flash without `audio_flash_hint()`: 14 overruns and 15 underruns came with it. Fixed in
  tune.c, not yet on the board.
- Wanted: guided tests paced by the player (each step waits for a click), not only timed.
