# The first session with a board

What to do when a Samplooler (rev C) is on the desk. Each step says what it finds out.

## 1. Plug it in, see it

```sh
slrig run boards            # the Samplooler's MIDI port and serial (a TWOFOLD is never listed)
```

It runs HUFF now, so it shows up under that name and the rig does not take it.

## 2. Flash the rig firmware (only when you say so)

The image is built in `~/Projects/samplooler/.rig/firmware/build` (branch `rig`).

1. Claude finds the board's own `/dev/serial/by-id/...` path once it is in download mode.
2. It flashes with `PORT=<that path> tools/flash.sh` from `~/Projects/samplooler/.rig`. The script's default picks
   the first Espressif port, and the TWOFOLD is an Espressif device: the path is always given explicitly.
3. After that, `slrig run boards` names it `Samplooler`.

The tuning table starts at v1, so it plays exactly as before until something is changed.

## 3. Still on the table (2 minutes)

```sh
slrig run test.run name=still        # bias, noise, which axis carries gravity
slrig run test.run name=axes         # the axis map: flat, nose down, right edge down, facing you
slrig run motion.snapshot
```

What to check:

- rates: motion 500 Hz, state 50 Hz, drops 0;
- CPU and heap (`slrig status`: `cpu`, `heap_kb`).

Two notes on the table:

- **The axis map was never checked on a board.** The defaults reproduce the old code's pitch and roll, and the
  `axes` test confirms the z axis too. If z is upside down, `tune.set motion.ax_z -z`.
- **The gyro bias learns itself** when the board lies still (`imu.auto_bias`). `tune.set imu.bias_keep 1` then
  `tune.save` keeps it.

## 4. Hands on

- `test.run name=steps`, `sweep`, `flicks`, `shake`, `roll`: each records, analyses and suggests.
- `session.compare a=v1 b=gyro`: the same recorded hand through the old tilt and the gyro's. It gives numbers and a
  plot.
- `tune.preset gyro`, then play. `tune.ab A/B` flips between two whole sets while you listen.
- `test.run name=encoder`, `rec`, `joystick`: the timings of every press and turn.

## 5. Audio

- `audio.measure secs=5`: levels, noise floor and clicks of what the board sends over USB.
- For the speed check:
  1. Set Input to USB.
  2. Play a 1 kHz tone into the board and record it as the take.
  3. Run `audio.measure tone_hz=1000`. It gives the real speed over time, to set beside `speed_now`.
