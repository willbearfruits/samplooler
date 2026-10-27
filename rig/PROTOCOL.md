# The rig's two protocols

1. **Board ↔ daemon:** MIDI SysEx on USB. The twin speaks the same bytes on stdin and stdout.
2. **Daemon ↔ clients** (viewer, CLI, MCP): JSON lines on a unix socket.

Everything here is a contract. Change it here first, then in the code.

---

## 1. Board ↔ daemon (SysEx, manufacturer 0x7D)

Existing messages, unchanged:

- host to board: `R` (reboot to download), `S` (status);
- `I`/`D`/`P`/`V`: settings, the app's line, proto 1;
- board to host: `L` (log text);
- CC 1 to 7 telemetry, and notes 60 to 64.

`I`'s answer gains `rig=1` when the firmware speaks what follows.

### Host → board

| msg | body (ASCII unless said) | meaning |
|---|---|---|
| `G` | `m1 q1 o1 e1` (any subset; empty = keepalive) | streams on/off: motion, state, screen, events. The motion rate is the tuning value `imu.rate`. Streams and key overrides stop 3 s after the last `G` or `K` (send `G` every second as a keepalive) |
| `T` | `l` | list the tuning table: one `t` per entry, then `t end=N` |
| `T` | `g12` | get value 12 → `t i=12 v=…` |
| `T` | `s12=6.5` | set value 12 (clamped, applied at once) → `t i=12 v=…` |
| `T` | `w` | save the table to NVS (`tune` blob) → `t saved=1` |
| `T` | `d` | back to the defaults (= today's behaviour, preset `v1`), not saved → full `t` list |
| `T` | `p1` | load preset 1 (0 = v1) → full `t` list |
| `J` | binary, below | injected motion: replaces the sensor until 0.5 s after the last `J`; a `J` with n = 0 gives the sensor back at once |
| `K` | `e+2` / `e-1` | the encoder turned by n detents |
| `K` | `E1` / `E0` | encoder switch down / up |
| `K` | `R1` / `R0` | REC down / up |
| `K` | `x2048 y1900` | joystick raw ADC override (0..4095); `x- y-` ends the override |

`J` body: `n` (1..6), then n samples, each 6 raw sensor values (ax ay az gx gy gz, the IMU's own LSB at the
current ranges, signed 16-bit) as three 7-bit bytes each (bits 15..14, 13..7, 6..0, two's complement),
so 18 bytes a sample. The board treats injected samples exactly like FIFO samples.

### Board → host

| msg | body | rate |
|---|---|---|
| `r` | raw motion, binary, below | each control tick while `m` is on |
| `q` | state, binary, below | each control tick (50 or 100 Hz) while `q` is on |
| `Q` | `fields=name:scale,name:scale,…` (ASCII) | once after `G` turns `q` on, and on `T` changes of rate |
| `o` | screen pages, binary, below | on change, at most `rig.screen_fps` |
| `e` | `name v=… ms=… text` (ASCII) | each gesture event |
| `t` | tuning table lines (ASCII, k=v) | answers to `T` |

**`r` raw motion.**

- Header: `seq` (7 bits), `frames` (5 × 7 bits, audio frames at 32 kHz of the first sample, MSB first),
  `rate code` (0 = 100 Hz, 1 = 250, 2 = 500, 3 = 1000), `ranges` (bits 0..1 accel 2/4/8/16 g, bits 2..3 gyro
  250/500/1000/2000 °/s), `flags` (1 injected, 2 FIFO overflow before these, 4 gyro present), `n`.
- Then n samples × 18 bytes, the same 3-byte packing as `J`.
- Values are the chip's own axes and raw LSB, before any offset or bias. The daemon converts with the
  ranges.

**`q` state.**

- Header: `seq`, `frames` (5 × 7 bits).
- Then one 14-bit signed value (two 7-bit bytes, high first) per field, in the order `Q` announced. The
  real value is `raw / scale`.
- The fields are listed in section 2 (`state`). Booleans are packed into `flags` (bit 0 frozen, 1 menu,
  2 injected, 3 REC down, 4 encoder down, 5 joystick present, 6 IMU present, 7 recording, 8 playing,
  9 joystick override).

**`o` screen.**

- `page` (0..7), `fps_drop` count (7 bits), then that page's 128 bytes (SSD1306 page format: byte x holds
  rows 8·page … 8·page+7, bit 0 on top), packed 7 bits per byte. That is 147 bytes of packed data.
- Only the changed pages are sent.

---

## Frames (axes)

- **Chip axes:** the MPU-6050's own. Only `r`, `J` and a session's `raw.txt` use them.
- **Device axes** are right-handed:
  - x points to the screen's right edge, y to its top edge (the nose: the edge away from a player reading the
    screen), z out of the screen.
  - Flat on a table with the screen up, acc = (0, 0, +1 g).
  - Nose down by θ, acc = (0, -sin θ, cos θ). Right edge down by φ, acc = (-sin φ, 0, cos φ).
  - The tuning values `motion.ax_*` map chip axes to device axes. The default is (-y, -x, -z), which gives the old
    firmware's pitch and roll.
  - Every `motion` push, plot and analysis uses device axes.
- **Angles:**
  - `pitch` = atan2(-a_y, √(a_x² + a_z²)), positive with the nose down. Nose down plays forward, faster.
  - `roll` = atan2(-a_x, √(a_y² + a_z²)), positive with the right edge down: the stutter. Left is the delay.
- **World:** z up.
  - `quat` and `ref_quat` rotate device-axis vectors into the world (q · v · q*). Flat, screen up and yaw 0 is the
    identity.
  - Nose down 30° is a turn about +x by -30°, q = (cos(-15°), sin(-15°), 0, 0).
  - Right edge down 30° is a turn about +y by +30°.
- **The 3D model:** KiCad's VRML frame (x right, y towards the board's top edge in the front view, z out of the
  top side), centred, is the device frame.
- **Yaw** has no absolute reference (no magnetometer). It drifts, and `motion.yaw_reset` zeroes it.

## 2. Daemon ↔ clients (unix socket, JSON lines)

Socket: `$XDG_RUNTIME_DIR/slrig/daemon.sock` (0600). One JSON object per line, UTF-8, both ways.

### Requests (client → daemon)

```json
{"id": 1, "op": "hello", "client": "view"}
{"id": 2, "op": "run", "cmd": "tune.set", "args": {"name": "tilt.dead", "value": 6.5}}
{"id": 3, "op": "subscribe", "streams": ["motion", "state", "screen", "events", "tune", "settings", "status", "log", "cmds", "prompt", "session"]}
{"id": 4, "op": "unsubscribe", "streams": ["motion"]}
```

### Replies

```json
{"id": 2, "ok": true, "result": {...}}
{"id": 2, "ok": false, "error": {"code": "tune.unknown", "message": "no tuning value 'tilt.ded'", "hint": "tune.list shows the names"}}
```

### Pushes (daemon → subscribed clients; no id)

| `ev` | fields | when |
|---|---|---|
| `status` | `source` ("board", "twin" or "none"), `board` {name, serial, version, rig}, `rates` {motion_hz, state_hz, screen_fps}, `drops` {motion, state, screen}, `recording` (bool), `osc` {out, in} | on change and once a second |
| `motion` | `t0` (s, board time of the first sample), `dt` (s), `acc` [[x,y,z] g …], `gyro` [[x,y,z] °/s …], `raw` (bool), `ref_quat` [w,x,y,z] (the host's own fusion, the latest), `injected` | at most 60 a second, batching every sample since the last push |
| `state` | see below | each `q` (50–100 Hz), the latest only if a client is slow |
| `screen` | `w` 128, `h` 64, `fb` (base64 of 1024 bytes, page format as above) | on change |
| `event` | `t`, `name`, `v`, `ms`, `text` | each gesture event |
| `tune` | `items` [tune item …] (all of them) | on subscribe and on any change |
| `settings` | `items` [setting item …] | on subscribe and on change |
| `log` | `text`, `level` | each board log line and daemon notice |
| `cmds` | `by` ("view", "cli", "mcp" or "osc"), `cmd`, `args`, `ok`, `ms` | after every command anyone runs (the viewer's log strip) |
| `prompt` | `test`, `step`, `of`, `text`, `secs` (left), `total` (the countdown's full length), `phase` ("ready", "go", "done") | during a guided test; `text` "" clears it |
| `session` | `state` ("idle" or "recording"), `name`, `secs` | on change, and once a second while recording |

**`state` fields** (also the `q` field names):

- `t`, `state` ("idle", "rec", "play", "stop"), `mode` ("TAPE", "GRAIN");
- `quat` [w,x,y,z] (the board's fusion);
- `pitch`, `roll`, `yaw` (°, after the axis map);
- `tilt_u` (-1..1, the control before the curve), `zone` ("dead", "fwd", "back", "detent");
- `speed_cmd`, `speed_now`, `pos` (0..1), `loop_s`;
- `delay`, `stutter` (0..1);
- `joy_x`, `joy_y` (-1..1, after shaping), `joy_rx`, `joy_ry` (raw ADC);
- `enc` (total detents), `enc_down`, `rec_down`, `frozen`, `menu`, `injected`, `joy_present`, `imu_present`,
  `recording`, `playing`;
- `in_peak`, `out_peak` (0..1), `in_clips`;
- `cpu` (%), `heap_kb`, `i2c_fail`, `bias` [x,y,z] (°/s, learned), `rest` (bool, the board thinks it is
  still);
- `loop_ms` (the last pass of the board's 10 ms UI loop: I2C, the screen, the streams), `wire_drops` (the rig's
  messages that did not fit on USB, since boot).

**Tune item:**

```json
{"i": 12, "name": "tilt.dead", "group": "tilt", "kind": "float", "min": 0, "max": 30, "step": 0.5,
 "def": 5, "value": 5, "unit": "°", "labels": [], "help": "no speed change inside this angle"}
```

- `kind` is one of "float", "int", "enum", "bool" or "action".
- An enum's `labels` name each value from `min`.

**Setting item** (the board's own settings, from `D`; they are stepped, never set to a number):

```json
{"i": 3, "tag": "p03", "name": "Tilt", "group": "TILT", "kind": "step", "text": "ON", "help": "Off: it plays forward at 1x, whatever the angle."}
```

- `kind` is "step" (`settings.step` with `delta` +1 or -1) or "act" (an action such as Save: `settings.step` with
  `delta` 1 runs it, and the item may carry `confirm`, text to show before running it).
- The groups come in this order: PLAY, TILT, SOUND, VOICE, SLOTS, LINK, SETTINGS.

### Commands

These are the registry's commands. `help` lists them with their arguments. This table is the first set.

| cmd | args | does |
|---|---|---|
| `status` | | the `status` push as a reply |
| `connect` | `source` ("auto", "board" or "twin"), `serial` | open the board (found by name, never a TWOFOLD) or start the twin |
| `disconnect` | | |
| `stream` | `motion`, `state`, `screen`, `events` (bools), `rate` | which streams run |
| `tune.list` / `tune.get` / `tune.set` / `tune.save` / `tune.reset` / `tune.preset` | `name`, `value`, `n` | the tuning table |
| `tune.ab` | `slot` ("A" or "B"), `store` (bool) | keep or recall two whole tuning sets |
| `settings.list` / `settings.step` | `index`, `delta` | the board's settings |
| `key` | `enc`, `enc_down`, `rec` | press and turn from the PC |
| `joy.override` | `x`, `y` (raw or null) | |
| `inject.take` / `inject.gesture` / `inject.stop` | `name`, `kind`, `secs`, `amp` | play a take or a synthetic gesture into the board |
| `record.start` / `record.stop` | `name`, `secs`, `audio` | a session |
| `session.list` / `session.info` / `session.analyze` / `session.replay` / `session.compare` | `name`, `tune` | |
| `test.list` / `test.run` / `test.abort` | `name` | guided tests (prompts on the viewer) |
| `audio.measure` | `secs`, `tone_hz` | capture the board's USB audio: level, clicks, pitch → speed |
| `screen.png` | `scale` | the OLED as a PNG file path |
| `view.shot` | `width`, `height` | an offscreen render of the viewer (slrig-view --shot) as a PNG path |
| `plot` | `session`, `fields`, `secs` | a PNG plot |
| `osc.config` | `out_host`, `out_port`, `in_port`, `on` | |
| `curve` | `n` (points, default 121) | the tilt → speed curve from the firmware's own `gesture.c` (built on the PC) with the current tuning and settings: `{"tilt": {"angle": [...], "speed": [...], "dead": d, "full": f, "detents": [...]}, "roll": {"angle": [...], "delay": [...], "stutter": [...]}}`. The viewer draws it and asks again after any `tune` or `settings` push |
| `gestures` | | every gesture the board knows: `[{"name": "enc.tap", "group": "enc", "rule": "released after 40..500 ms", "tune": ["enc.tap_min", "enc.tap_max"], "count": 3, "last_t": 12.3}]` |
| `log.tail` | `n` | |

---

## 3. Sessions on disk

A recording is a folder, `lab/rig/sessions/<YYYY-MM-DD>-<HHMMSS>-<name>/` (or `$SLRIG_SESSIONS/...`). Every file is
plain, and the time `t` is seconds from the session's start on the board's clock (audio frames / 32000).

| file | what |
|---|---|
| `meta.json` | `name`, `started` (ISO), `secs`, `source` ("board" or "twin"), `board` {name, serial, version}, `tune` {name: value} at the start, `settings` {name: text}, `imu` {rate_hz, acc_lsb, gyro_lsb, axis_map [ax_x, ax_y, ax_z codes 0..5]}, `test` (null, or {name, steps: [{t0, t1, label, prompt}]}), `injected` (bool), `notes` |
| `motion.csv` | `t,ax,ay,az,gx,gy,gz,flags`: device axes (the axis map applied), g and deg/s, raw (no offset or bias removed); flags 1 injected, 2 FIFO overflow before this sample, 8 samples lost before this one |
| `raw.txt` | the same rows as the chip's raw LSB, `ax ay az gx gy gz` a line: what `twin --calc replay` reads |
| `state.csv` | `t` and every `state` field by name (section 2), one row per `q` |
| `events.jsonl` | `{"t", "name", "v", "ms", "text"}` a line |
| `screen.bin` | records of an 8-byte little-endian float64 `t` and the 1024-byte frame (page format), one per change |
| `audio.wav` | optional: the board's USB output, stereo 16-bit, 32 kHz (left the mix; right the raw input when `usb.out` is 1) |
| `analysis.json` | written by `session.analyze` |
| `replay-<label>.csv` | written by `session.replay`: the twin's per-tick output for one tuning set |
