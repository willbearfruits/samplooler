# MANTRA

A singing voice made of formants, and a seated Buddha who sings it: monks chanting OM, overtone singing (khoomei,
sygyt, kargyraa), and "Daisy Bell" the way the IBM 7094 sang it in 1961. Behind him, when the sound calls for it, a
wall of amps: one chord held through a fuzz, swelling, with feedback singing over it, the way Sunn O))) and Earth do.
It sings by itself; tilt the box to open and close its mouth, roll it for the pitch, turn it flat like a prayer wheel
to walk the overtone up the harmonics, pump it for breath, knock on it to strike a singing bowl. Your own voice can
go through the same mouth.

Firmware for the Samplooler Stack (ESP32-S3).

## Playing

1. Switch it on: the Buddha chants OM.
2. Turn the knob: the chant turns into "Daisy Bell", first hummed, then sung by the choir, then one voice alone.
3. In the menu, SOUND > SOUND picks the chant: OM, MONKS, OM MANI, KHOOMEI, SYGYT, KARGYRAA, DAISY, DAISY 8-BIT,
   VOWELS, HUM ALONG, and three with the amps up: SUNN, DOOM DAISY, ORGAN. MIX > DRONE brings the amps into any
   sound.
4. Tip it forward and back: the vowels. Roll it: the pitch. Turn it flat on the table: the overtone. Pump it: the
   breath. Knock on it: the bowl.
5. Hold REC to loop what it sings; hold again to add a layer.

## Controls

| | |
| - | - |
| turn the knob | from the chant to Daisy Bell |
| press the knob | the menu |
| tip forward / back | the mouth: OO OH AH EH EE |
| roll | the pitch |
| turn it flat | the overtone |
| pump it | the breath |
| stick | bend, vibrato, hum |
| knock | the singing bowl (and the amps' chord) |
| hold REC / tap REC | loop / stop and play |

"Daisy Bell" (Harry Dacre, 1892) is in the public domain. The singing engine is MANTRA's own; it does not use
SAM or any other speech program's code. Inspired by the IBM 7094 performance of 1961 and by the Delay Lama plugin.

## Build

ESP-IDF v5.5.1:

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name, never touches a TWOFOLD
```

`tools/sim/run.sh` runs the simulator and writes screens, animations and sound to `tools/sim/out/`.

## Status

Built 2026-10-17 and 18 and ran on a board: chanting OM it used about a third of the processor. The wall of amps
was added on the 18th and has only been heard in the simulator (160 checks); it is WRECK's amp, which runs on a
board. A speech recogniser understands about half of the Daisy chorus at a brisk tempo, fewer words at the slow
default with the waltz under it.
