# PIXEL

A chiptune machine. You write four parts on a grid: drums, bass, chords and a lead. You chain the bars into a song.
The knob picks which of eight old sound chips plays it, from a one-bit beeper to the Mega Drive's FM.

Firmware for the Samplooler boards (ESP32-S3). Writing is comfortable with the joystick of rev C, or with the two
knobs of rev E (the Vessel): the left one moves the cursor, the right one writes. On a board with neither, the one
knob does both jobs.

Rev E has no joystick. It has a second knob on the left and a sensor that measures how high a hand is held over the
board. PIXEL uses both since 2026-11-17. This is tested in the simulator only: it has run on no rev E board.

## Playing

1. Switch it on and tap REC. The first song plays. Tap REC again to stop.
2. Turn the knob. The same song plays on another chip: BEEPER, ATARI, AY, NES, GAME BOY, C64, AMIGA, MEGA.
3. Push the stick to mute a part: up the lead, right the chords, down the bass, left the drums. Push again to bring
   it back. On rev E, hold a hand over the middle of the board instead: as it comes down the lead drops out, then
   the chords, then the bass. The drums stay. Take the hand away and they are back.
4. Tip the far edge down and the cartridge slips: the tune glitches. Roll the box left or right and everything bends.
5. Press the knob for the menu. SONG has seven songs and EMPTY, a blank one to write in.
6. On rev E the left knob is the tempo: one click is one BPM, from 60 to 240.

| rev E | |
| - | - |
| left knob | the tempo, one BPM a click, 60 to 240. It is TUNE > TEMPO, so it is saved with the song. The screen shows the BPM for a moment |
| a hand over the sensor | the breakdown. Under 19 cm the lead is out, under 14 cm the chords too, under 9 cm the bass too. The drums play on. Each part comes back 2 cm higher than it went (21, 16, 11 cm). When the hand leaves, all are back after about half a second |
| push the left knob | REC |

## Writing

Hold the knob. The screen becomes one bar, sixteen steps across. There are five rows: SONG, LEAD, CHORDS, BASS, DRUMS.
Hold the knob again to go back.

| | |
| - | - |
| stick left / right | the cursor along the steps, and on into the next bar |
| stick up / down | the row |
| left knob (rev E) | the cursor along the steps, one step a click, and on into the next bar |
| hold the right knob down and turn the left one (rev E) | the row, one a click |
| turn the knob | the note higher or lower (an empty step gets a note); the chord; which drum; which pattern |
| press the knob | nothing, a note, an accented note, nothing. On a note's tail: the note ends there |
| press and turn | the note longer or shorter; the chord's kind (plain, seventh, sus, fifth) |
| tap REC | play this bar round and round; on the SONG row, the song from here. Tap again to stop |
| hold REC a second | empty this row's part in this bar |

On rev E "the knob" in this table is the right knob, and REC is the left knob's push. A hand over the sensor does
nothing while you write.

A note's tail can glide: put the cursor on the tail and turn. The note slides to that pitch from there.

The SONG row is the bars in order. Each bar plays a pattern, A to P. Press to play a bar once more. Press and turn
right to give a bar a pattern of its own (a copy you can change without changing the others). Press and turn left to
take the bar out.

Stuck? BAR > DICE in the menu throws an idea into the row you were on: a beat, a bass line, a tune or a chord.

Notes are rows of the song's scale, so they are always in key. TUNE > SCALE has ten scales; ALL NOTES gives every
semitone.

## Saving

A song is saved like a sound on the other instruments: menu, SONG, SAVE AS, one of eight places. A changed song shows
a star and keeps its changes through a power cycle. REVERT goes back to the saved one. The factory songs cannot be
overwritten: change one and save it as your own.

## The consoles

| | chip | how it plays the four parts |
| - | - | - |
| BEEPER | the ZX Spectrum's one-bit speaker | two thin voices; the drums stop everything for a moment |
| ATARI | TIA (Atari 2600) | two voices, 32 pitches each: out of tune by design |
| AY | AY-3-8910 (Spectrum 128, Atari ST) | three squares, noise, a buzzing bass |
| NES | 2A03 | two pulses, the triangle bass, noise, sampled drums |
| GAME BOY | DMG | two pulses, the wave channel, noise |
| C64 | SID 6581 | three voices and the filter; fast arpeggios |
| AMIGA | Paula | four sample channels, as a tracker module |
| MEGA | YM2612 and the PSG (Mega Drive) | FM bass, lead and real chords, sampled drums |

Each console has its own four lead sounds, four basses, chord sounds and drum kits (LEAD > SOUND and so on). The
chips are models written for PIXEL; the samples on the AMIGA, the MEGA and the NES are synthesized, not recordings.

## With other boards

PIXEL joins the radio link like the other instruments (LINK in the menu). Next to a board that keeps a loop it
follows that tempo; a start waits for the next bar line. The chord that sounds is sent to boards that follow chords.

## MIDI

Notes from a keyboard on USB play the lead. Every setting is on a CC (the manual has the list). Program change picks
a song.

## Build

ESP-IDF v5.5.1:

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name, never touches a TWOFOLD
```

`tools/sim/run.sh` runs the simulator and writes screens, animations and sound to `tools/sim/out/`.
`tools/conlab/run.sh` plays a bench tune through each console and checks it.

## Status

It runs on a board: flashed on a rev C Stack on 2026-11-10, it starts, plays and keeps up (13 % of the CPU on the
AMIGA console, no dropped audio after the start). It has not been played much yet. The load of the other seven
consoles, long sessions and the link with a second board have only been checked in the simulator.
[docs/MANUAL.md](docs/MANUAL.md) has every setting.

Rev E's left knob and hand are tested in the simulator only. They have run on no board: no rev E has been built
yet. The heights at which the parts drop out are untried on a real hand. What a rev E cannot do that a board with a
joystick can is in the manual ("Rev E").

The two classical songs are Grieg's "In the Hall of the Mountain King" and Mozart's "Rondo alla Turca", set by hand.
The other five were written for PIXEL.
