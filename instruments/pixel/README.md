# PIXEL

A chiptune machine. You write four parts on a grid: drums, bass, chords and a lead. You chain the bars into a song.
The knob picks which of eight old sound chips plays it, from a one-bit beeper to the Mega Drive's FM.

Firmware for the Samplooler Stack (ESP32-S3). It needs the joystick to write comfortably; without one the knob does
the stick's work.

## Playing

1. Switch it on and tap REC. The first song plays. Tap REC again to stop.
2. Turn the knob. The same song plays on another chip: BEEPER, ATARI, AY, NES, GAME BOY, C64, AMIGA, MEGA.
3. Push the stick to mute a part: up the lead, right the chords, down the bass, left the drums. Push again to bring
   it back.
4. Tip the far edge down and the cartridge slips: the tune glitches. Roll the box left or right and everything bends.
5. Press the knob for the menu. SONG has seven songs and EMPTY, a blank one to write in.

## Writing

Hold the knob. The screen becomes one bar, sixteen steps across. There are five rows: SONG, LEAD, CHORDS, BASS, DRUMS.
Hold the knob again to go back.

| | |
| - | - |
| stick left / right | the cursor along the steps, and on into the next bar |
| stick up / down | the row |
| turn the knob | the note higher or lower (an empty step gets a note); the chord; which drum; which pattern |
| press the knob | nothing, a note, an accented note, nothing. On a note's tail: the note ends there |
| press and turn | the note longer or shorter; the chord's kind (plain, seventh, sus, fifth) |
| tap REC | play this bar round and round; on the SONG row, the song from here. Tap again to stop |
| hold REC a second | empty this row's part in this bar |

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

It runs in its simulator only. It has not been on a board yet, so nothing here has been heard through the Stack's
own output or played with real hands. [docs/MANUAL.md](docs/MANUAL.md) has every setting.

The two classical songs are Grieg's "In the Hall of the Mountain King" and Mozart's "Rondo alla Turca", set by hand.
The other five were written for PIXEL.
