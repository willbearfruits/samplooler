# WRECK

A breakcore machine that turns into a doom machine. Turn the knob: from a break chopped, rolled and crushed at
twice the tempo, through hardcore and sludge, down to one chord held under a break slowed into thunder. It plays by
itself; tilt, roll, the stick and knocks push it around. Next to a [BAND](../band) or a [SING](../sing) it stays on
their beat.

Firmware for the Samplooler's ESP32-S3 boards (the Stack, the Mini, the breadboard kit). Not run on a board yet
(see Status).

## Playing

1. Switch it on: it plays a break, chopped.
2. Turn the knob. Four zones: BREAKCORE (x2 the tempo), HARDCORE (x1), SLUDGE (x1/2), DRONE (x1/4).
3. Tip it forward for more chaos, back to drag the tape. Roll left to reverse, right to stutter.
4. The stick: up a roll, down a dive, left crush, right drive. Knock on the box for a fill.
5. Hold the knob to chop the break yourself on a 16-step grid.
6. Hold REC to sample a break (the mic, the computer over USB, or WRECK itself). Tap REC to stop and start.

## The breaks

There is no recording of anyone else's break in WRECK (the famous ones are under copyright). The built-in breaks
are a synthesized kit playing the classic patterns. To chop a real one, play it into the board, from the mic or
over USB (the board is a USB sound card), and hold REC.

## Controls

| | |
| - | - |
| turn the knob | from breakcore to doom |
| press the knob | the menu |
| hold the knob | the step grid |
| tip forward / back | chaos / drag the tape |
| roll left / right | reverse / stutter |
| stick | up roll, down dive, left crush, right drive |
| knock | a fill |
| hold REC | sample a break |
| tap REC | stop / start |

## Sounds

WRECK, SNARES, DRILL, GABBA, GRIND, BAROQUE, WIZARD, EARTH, DRONE, THUNDER: each sets both ends of the knob. The
list is in [docs/MANUAL.md](docs/MANUAL.md).

## Build

ESP-IDF v5.5.1:

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name, never touches a TWOFOLD
```

`tools/sim/run.sh` runs the simulator and writes screens, animations and sound to `tools/sim/out/`.

## Status

Built 2026-10-16. Not run on a board yet and not heard on one: everything so far is the simulator (153 checks) and
WAVs from it. In the simulator it needs less than half of what SING and BAND need at their busiest, so it should fit.
A new built-in break takes a few seconds to print on the board.
