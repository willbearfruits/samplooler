# WRECK 2

[WRECK](../wreck) with the dice taken out of the machine and put in your hand. The same breakcore-to-doom engine:
turn the knob from a break at twice the tempo, through hardcore and sludge, down to one chord held under a break
slowed into thunder. But nothing is chopped unless you do it. Roll the box right for WRECK's chaos, as much as you
like; tap BOOT and the bar you just heard is caught and loops; hold the knob and fix it a step at a time. Tip the
box to slow the break down, until the Amen is sludge. Next to another board on the radio link (a [MANTRA](../mantra),
a [HUFF](../huff), a [BAND](../band), the Samplooler) it stays on the same beat and plays in the same key.

Firmware for the Samplooler Stack (ESP32-S3).

## Playing

1. Switch it on: it plays a break, straight, at twice the flock's tempo.
2. Turn the knob. Four zones: BREAKCORE (x2), HARDCORE (x1), SLUDGE (x1/2), DRONE (x1/4). The whole break plays in
   every zone, slowed with the tempo; the amp chugs, riffs and drones under it.
3. Tip it back: the break slows, in whole steps so it stays on the beat (and a bend in between). Tip it forward: faster.
4. Roll it right: the dice. WRECK's random edits, more the further you roll. Roll left: reverse and smear.
5. The stick: up a roll, down a dive, left crush, right drive. Knock on the box for a fill.
6. Tap BOOT: CATCH. The last bar, as it just played, hands and dice included, goes into the grid and loops. Tap again
   to let it go.
7. Hold the knob: the grid. Three pages in one row: the DRUMS (16 steps, a slice and a trick each), the amp's RIFF
   (8 notes of the scale), the BARS (an 8-bar cycle: crush, drive, a filter sweep, reversed, half speed, a tape stop
   or the drums out, one per bar).
8. Hold BOOT to sample a break (the mic, the computer over USB, or WRECK 2 itself). The machine stops and starts
   from the menu (WRECK > STOP / GO).

## Controls

| | |
| - | - |
| turn the knob | from breakcore to doom |
| press the knob | the menu |
| press the knob twice | the knob turns the volume, until it rests 3 s or you press it again |
| hold the knob | the grid: drums, riff, bars |
| tip back / forward | the break slower / faster |
| roll left / right | reverse / the dice |
| stick | up roll, down dive, left crush, right drive |
| knock | a fill |
| tap REC (BOOT) | catch the bar, or let it go |
| hold REC (BOOT) | sample a break |

## The notes

The amp plays the flock's key, scale and chord, which is what the other boards share on the link. AMP > NOTES:
FOLLOW (the chord's root on the kicks: plays with anything), RIFF (your eight notes from the grid, as steps of the
scale from the chord's root, so they move with the chords), STONER (WRECK's riffs: TRITONE, CREEP, SMOKE, BENDS,
TWANG, POUND).

## The breaks

There is no recording of anyone else's break in WRECK 2 (the famous ones are under copyright). The built-in breaks
are a synthesized kit playing the classic patterns. To chop a real one, play it into the board, from the mic or
over USB (the board is a USB sound card), and hold BOOT.

## Build

ESP-IDF v5.5.1:

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name, never touches a TWOFOLD
```

`tools/sim/run.sh` runs the simulator and writes screens, animations and sound to `tools/sim/out/`.

## Status

Built 2026-10-21 from WRECK. Simulator only so far: 191 checks (the catch, the bars, the riff, the tilt's speed,
a long run in each zone with everything going). Not yet run on a board.
