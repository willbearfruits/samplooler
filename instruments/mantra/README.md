# MANTRA

A singing voice made of formants, and a seated Buddha who sings it: monks chanting OM, overtone singing (khoomei,
sygyt, kargyraa), and "Daisy Bell" the way the IBM 7094 sang it in 1961. Behind him, when the sound calls for it, a
wall of amps: one chord held through a fuzz, swelling, with feedback singing over it, the way Sunn O))) and Earth do.
It sings by itself; tilt the box to open and close its mouth, roll it for the pitch, turn it flat like a prayer wheel
to walk the overtone up the harmonics, pump it for breath, knock on it to strike a singing bowl. Your own voice can
go through the same mouth.

Firmware for the Samplooler Stack (ESP32-S3).

Rev E of the board (the Vessel) has no joystick. It has a second knob on the left and a sensor that measures how
high a hand is held over the board. Since 2026-11-17 MANTRA uses both: the left knob is the note, the hand is a
hand over the Buddha's mouth. This is tested in the simulator only. No rev E board has run it.

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
| press the knob twice | the knob turns the volume, until it rests 3 s or you press it again |
| tip forward / back | the mouth: OO OH AH EH EE |
| roll | the pitch |
| turn it flat | the overtone |
| pump it | the breath |
| stick (boards with a joystick) | bend, vibrato, hum |
| left knob (rev E) | the note: one click is one semitone, up to an octave up or down, on top of the roll. It moves the amps too. It is HANDS > NOTE, and it stays where you leave it |
| a hand over the sensor (rev E) | a hand over his mouth: the lower the hand, the more the mouth shuts into a hum. Fully down (about 6 cm) he hums with the mouth shut, and the amps sink an octave. HANDS > HAND can make it the vibrato instead |
| knock | the singing bowl (and the amps' chord) |
| hold REC / tap REC (rev E: push the left knob) | loop / stop and play |

Rev E's two controls are tested in the simulator only. They have run on no board. What a rev E cannot do that a
board with a joystick can is listed in the manual ("Rev E").

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

Rev E's left knob and hand were added on 2026-11-17 and checked in the simulator only (29 checks of their own). No
rev E board has run MANTRA.
