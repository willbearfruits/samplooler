# Journal

## 2026-10-27 01:00

Plan approved. The user added: every input tuned (gyro, accelerometer, mic, joystick, encoder) and every
gesture visible and tunable. Asked mid-build: yes, the Samplooler has only ever read the accelerometer
(the gyro is powered and never read; tilt lags about 75 ms: a 0.25 one-pole at 50 Hz plus the chip's 44 Hz
filter).

## 2026-10-27 02:20

**Built overnight:**

- the firmware with the gyro and the live tuning table;
- the twin;
- the daemon with 46 commands, OSC and the MCP server;
- analysis, guided tests and synthetic gestures (a helper agent);
- the 3D viewer (a helper agent).

**Turns on the way:**

- **Bit-for-bit v1 was too strong a claim under `-ffast-math`.** Angles are equal to the bit and decisions equal;
  values are within 3e-6.
- **The first axis convention was left-handed.** The viewer agent found it, and it is now x right, y to the nose,
  z out of the screen.
- **An injection left a 0.5 s hole in the motion stream.** A `J` with no samples now ends it at once.

**First result, on the twin:** the same shake moves v1's pitch 21.8° and COMP's 2.2°.

Screenshots: `view-*.png`, `shake-v1-vs-gyro.png`, `oled-mirror-twin.png`.
