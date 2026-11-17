# PIXEL manual

PIXEL is a chiptune machine. A song is four parts written on a grid of sixteenth steps: DRUMS, BASS, CHORDS and
LEAD. Bars are chained into a song. One of eight old sound chips plays it, and the knob changes which.

It has run on a board (a rev C Stack, 2026-11-10) but has not been played much: most of what is here was checked in
the simulator. What it does with rev E's left knob and hand sensor ("Rev E" below) has run on no board at all.

## Playing

| | |
| - | - |
| tap REC | play the song from the start; tap again to stop |
| turn the knob | the console: BEEPER, ATARI, AY, NES, GAME BOY, C64, AMIGA, MEGA |
| press the knob | the menu |
| press the knob twice | the knob turns the volume, until it rests 3 s or you press it again |
| hold the knob | the editor (below); hold again to come back |
| stick up / right / down / left | mute or bring back the LEAD / CHORDS / BASS / DRUMS |
| left knob (rev E) | the tempo, one BPM a click, from 60 to 240 |
| a hand over the sensor (rev E) | the breakdown: as the hand comes down the LEAD drops out, then the CHORDS, then the BASS; they come back when it rises or leaves |
| tip the far edge down | the cartridge slips: the tune glitches, more the further you tip |
| roll left / right | everything bends down / up (HANDS > BEND sets how far) |

The screen draws the song as a level that scrolls by. The lead's notes are platforms and a runner hops along them.
The bass is blocks on the ground. A kick shakes the ground, a snare throws sparks, hats twinkle as stars. The chord's
name is in the top left corner. On the right are the chip's own voices, each with the wave it makes now, and four
lamps for the parts: L, C, B, D. Along the bottom of the header are the bar's sixteen steps.

A new console starts on the next driver tick after a 3 ms fade, so the knob can be turned while the song plays.

BOOT is a second REC button on every board. On rev E the left knob's push is REC.

## Writing

Hold the knob. One bar is on the screen, sixteen steps across, with a mark every four. There are five rows; the
stick's up and down moves between them: SONG, LEAD, CHORDS, BASS, DRUMS. The header shows the row, the pattern's
letter, which bar of the song this is, and what is under the cursor.

On rev E the knob in this table is the right knob. The left knob does the stick's left and right, and the row is
changed by holding the right knob down and turning the left one ("Rev E" below).

| | LEAD, BASS | CHORDS | DRUMS | SONG |
| - | - | - | - | - |
| stick left / right | the cursor along the steps, on into the next bar | the same | the same | from bar to bar |
| left knob (rev E) | the same, one step a click | the same | the same | the same |
| turn the knob | the note higher or lower; an empty step gets a note | which chord, I to VII; OFF below I | which drum is in hand | which pattern, A to P, the bar plays |
| press the knob | nothing, a note, an accented note, nothing | put the chord, or take it away | nothing, a hit, an accented step, nothing | the bar once more, after itself |
| press and turn | the note longer or shorter | the chord's kind: plain, seventh, sus, fifth | | right: a pattern of its own for this bar. Left: the bar leaves the song |
| tap REC | this bar round and round | the same | the same | the song from this bar |
| hold REC 1 s | empty the part in this bar | the same | the same | |

**Notes.** A note is a row of the song's scale, so it is always in key. It lasts one step until you make it longer.
On a note's tail (any step after its first) a turn makes the note glide to another pitch from there, and a press ends
the note before that step. LEAD > SLIDE sets how long a glide takes. LEAD > LENGTH and BASS > LENGTH set how much of
a note's last step sounds: short for separate notes, 100 for joined ones.

**Chords.** A chord sounds from its step until the next chord or an OFF. Its notes come from the scale: in a minor
key I is minor and III is major. The name shows which: "IIm" is minor, "VIIo" diminished. How a chord is played is a
setting, not something you write: CHORDS > ARP breaks it up fast or slowly (chips with one voice to spare play a
chord as a fast arpeggio; the MEGA plays it whole), CHORDS > SHAPE is the order of its notes, CHORDS > RHYTHM holds
it or plays it as stabs in a pattern.

**Drums.** Five drums: KICK, SNARE, HAT, OPEN HAT and TOM. Turn to pick one, press to put it on the step. An accent
belongs to the step: it makes everything hit there louder.

**The song.** The SONG row is the bars in order, up to 64. Each bar plays one of sixteen patterns, A to P. The same
pattern can be used many times: change it and every bar that plays it changes. To change one bar only, press and
turn right on it: it gets a copy under the next free letter. The song goes round when it reaches the end.

BAR in the menu works on the bar the cursor was last on. DICE throws an idea into the row you were on: a beat on
DRUMS, a bass line on BASS, a tune on LEAD, another chord on CHORDS. The bass and the tune follow the bar's chords, so
put the chords first. Throw again until you like it, then change it by hand. COPY, PASTE and CLEAR move or empty a
whole bar.

Without a stick and without a left knob (the Mini) the knob walks the cursor step by step and on into the next row,
and press and turn changes what a turn would have.

## Rev E (the Vessel)

Rev E has no joystick. It has a second knob on the left, whose push is REC, and a sensor in the middle of the board
that measures how high a hand is held over it. The same firmware runs on both boards and finds out by itself which
one it is on. Nothing changes on a board with a joystick.

All of this is tested in the simulator only. No rev E board has run it. The numbers below are the simulator's, and
how the hand feels is not known yet.

| | |
| - | - |
| left knob, in play | The tempo. One click is one BPM. It stops at 60 and at 240. It steps TUNE > TEMPO itself, so the tempo is saved with the song, shows as an edit, and goes out on CC 22. The screen shows the BPM for a moment |
| left knob, with other boards | The new tempo is the whole flock's, as when TEMPO is changed in the menu. If another board's tempo is playing, the knob starts from that tempo, not from the song's own. While a board keeps a loop the knob changes nothing and the screen says A LOOP HAS THE TEMPO |
| a hand, in play | The breakdown. The hand is read from 25 cm down to 4 cm. Coming down, the LEAD drops out at 19 cm, the CHORDS at 14 cm and the BASS at 9 cm. The DRUMS always play. Going up, each part comes back 2 cm higher than it went: the BASS at 11 cm, the CHORDS at 16, the LEAD at 21. So a hand that shakes a little at a border does not switch a part in and out |
| a hand taken away | The parts come back in turn, the BASS first. All are back after about half a second |
| left knob, in the editor | The cursor along the steps, one step a click, and on into the next bar at the bar's ends, as the stick's left and right. On the SONG row it goes from bar to bar |
| right knob, in the editor | Everything the knob does on a board with a stick: the note, the chord, the drum, the pattern; press; press and turn |
| the row, in the editor | Hold the right knob down and turn the left one: one click is one row, to the right is down the screen. Once the left knob has turned, that press puts no note and does not leave the editor, however long it lasts. Start turning within half a second: the right knob held 0.6 s by itself still leaves the editor |
| a hand, in the editor | Nothing |

A part dropped by the hand is muted the way the stick mutes it: it stops at the next step, and its lamp on the screen
goes out. The hand only holds parts out. It never changes what was muted before it came, and nothing stays muted
when it has gone. SYSTEM > HAND OFF ignores the sensor. SYSTEM > LEFT KNOB OFF ignores the left knob: the right knob
then walks the cursor as on a board without a stick.

To leave the editor, hold the right knob without touching the left one, as on any board.

What a rev E cannot do that a board with a joystick can:

- Mute one part by itself and leave it muted. The hand takes the parts in one order (LEAD, CHORDS, BASS) and only
  while it is there. The DRUMS cannot be muted. The slow way round is each part's LEVEL in the menu (LEAD > LEVEL
  and so on, also on MIDI): 0 is silence. LEVEL is saved with the song.
- HANDS > STICK does nothing there.

## Songs

SONG in the menu is the list: your last edit (if there is one), the factory songs, then yours.

| | |
| - | - |
| FIRST LEVEL | A minor, 150. Written for PIXEL |
| HIGH SCORE | C major, 132 |
| DEEP CAVE | D minor, 92, slow |
| LAST BOSS | E harmonic minor, 172 |
| SEA BREEZE | F major, 108, with swing |
| MOUNTAIN KING | Grieg, "In the Hall of the Mountain King" (1875) |
| TURKISH MARCH | Mozart, "Rondo alla Turca" (about 1783) |
| EMPTY | one empty bar to write in |

A song carries its notes and its settings: tempo, key, scale, the console, each part's sound and level. A changed
song shows a star. Choose another and the changed one waits as MY EDIT. SAVE AS stores it in one of eight places.
REVERT goes back to the saved song. Changes that are not saved yet survive switching off, once they have been in
place a few seconds. SYSTEM > RESET ALL removes your songs.

## The consoles

<!-- consoles -->
Each console is a model of the chip (its own counters, registers, noise generators and volume steps, with every
edge band-limited so nothing folds back at 32 kHz) and a sound driver that uses it the way that machine's music did.
Pitch goes through the chip's own period register, so high notes are a few cents off where the real chip's were.

**BEEPER** (ZX Spectrum 48K, 1982). One bit on a speaker: two pulse counters, their pulses OR'ed. No volume: a
part's level and a note's shape are the pulse's width. The bass has one channel; the lead and the chords share the
other, tick by tick. A drum stops everything for its few milliseconds, which is the beeper's own stutter. No echo.

**ATARI** (Atari 2600, TIA, 1977). Two voices, each a waveform from the chip's shift registers, a 5-bit divider and
a 4-bit volume. Only 32 pitches per waveform, so every note takes the nearest one: it is out of tune by design, about
a quarter of a semitone on average. The bass shares its voice with the drums, the lead with the chords. Slides and
vibrato come out as steps. No echo.

**AY** (AY-3-8910 as in the Spectrum 128; the Atari ST's chip is its twin). Three squares, one noise generator, the
envelope generator. Channel A is the lead, B the chords' arpeggio (the echo when no chord sounds) and most drums, C
the bass and the kick. BUZZER is the envelope's sawtooth used as the bass's waveform; it has no volume on the chip,
so BASS > LEVEL scales it here (a cheat).

**NES** (2A03, 1983). Pulse 1 is the lead, pulse 2 the chords (else the echo), the triangle the bass and the kick,
the noise channel the snare and hats. The DPCM kits play a kick, a snare and a tom as 1-bit samples made when it
starts. The triangle has no volume on the chip: BASS > LEVEL scales it here (a cheat), and its four sounds differ
only in pitch tricks.

**GAME BOY** (DMG, 1989), in the manner of LSDj. Pulse 1 is the lead, pulse 2 the chords (else the echo), the wave
channel the bass, noise the snare and hats. The kick is a fast fall on the wave channel or on pulse 2, by the kit.
FRAMES steps through eight waveforms by the tick.

**C64** (SID 6581, 1982), in the manner of Rob Hubbard and Martin Galway. Voice 1 is the lead: a pulse with a moving
width, a synced sawtooth, a pulse through the filter, a ring modulated bell. Voice 2 is the bass through the low-pass
filter, and the drums on the same voice as waveform tables (noise, pulse and triangle in turn), after which the bass
comes back. Voice 3 is the chords' fast arpeggio, or the echo.

**AMIGA** (Paula, 1985), as a tracker module. Four channels of 8-bit samples held between steps, through the A500's
output filter. Channel 1 is the drums (a hit cuts the one before), 2 the bass, 3 the chords, 4 the lead. SAMPLED
plays a whole chord recorded as one sound on its root, the module trick. Every sample is made when it starts: none
is a recording.

**MEGA** (Mega Drive: YM2612 FM and the PSG, 1988). Six FM channels of four operators each. FM 1 is the bass, FM 2
the lead, FM 3 to 5 play the chord whole (the one console with real chords: ARP SLOW breaks it across the three, the
other settings play it together; a four-note chord drops its fifth). Channel 6 is the chip's 8-bit DAC with a kick
and a snare made when it starts. The PSG's noise is the hats, its squares the echo and the tom. It goes through the
first Mega Drive's output filter, which makes it darker than the others.

Where a console has one voice for the drums and two fall on a step, the snare wins, then the kick, the tom, the open
hat, the hat.

<!-- sounds -->

| | LEAD > SOUND | BASS > SOUND | CHORDS > SOUND | DRUMS > KIT |
| - | - | - | - | - |
| BEEPER | THIN, PULSE, PLUCK, SWEEP | BUZZ, FAT, PLUCK, OCTAVES | THIN, PULSE, PLUCK, SWEEP | CLICK, DARK |
| ATARI | PURE, BUZZ, PLUCK, TREMOLO | BUZZ, LOW, GROWL, PLUCK | PURE, BUZZ, PLUCK, BUZZ PLK | NOISE, BUZZ |
| AY | SQUARE, BELL, SOFT, OCTAVES | BUZZER, SQUARE, PHASE, PLUCK | ARP, SOFT, PLUCK, OCTAVES | NOISE, TONE |
| NES | SQUARE, HOLLOW, PLUCK, SWEEP | TRIANGLE, PLUCK, WOBBLE, OCTAVES | ARP, SOFT, PLUCK, SWEEP | 2A03, DPCM, METAL, DPCM MTL |
| GAME BOY | PULSE, SOFT, WOBBLE, ZAP | WAVE, ROUND, SQUARE, FRAMES | ARP, SOFT, PLUCK, SWEEP | WAVE, PULS MTL, WAVE MTL, PULSE |
| C64 | PULSE, SYNC SAW, FILTER, BELL | PLUCK, SAW, TRIANGLE, RUBBER | PULSE, SAW, SOFT, THIN | HARD, SOFT |
| AMIGA | SAMPLED, SQUARE, SAW, MALLET | FILTER, SQUARE, FM, TRIANGLE | SAMPLED, ARP, PAD, SAW ARP | MACHINE, LO-FI |
| MEGA | BRASS, SQUARE, FLUTE, BELL | SYNTH, ROUND, SLAP, ORGAN | E.PIANO, STRINGS, ORGAN, CLAV | DEEP, HARD |

<!-- /sounds -->

A name that appears once in a cell with fewer than four names means the other numbers repeat it.
<!-- /consoles -->

## Settings

Every setting is in the menu and on a MIDI CC. LINK, HANDS and SYSTEM belong to the board; the rest are saved with
the song.

<!-- tables -->

**TUNE**

| Setting | Values | CC | |
| - | - | - | - |
| TEMPO | 60 to 240 bpm | 22 | The song's tempo |
| SWING | 0 to 50 % | 23 | Every other step is late |
| KEY | C, C#, D, D#, E, F, F#, G, G#, A, A#, B | 24 | Moves the whole song up |
| SCALE | MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR, PENTA MAJOR, PENTA MINOR, BLUES, ALL NOTES | 25 | The notes of the rows |

**CONSOLE**

| Setting | Values | CC | |
| - | - | - | - |
| CONSOLE | BEEPER, ATARI, AY, NES, GAME BOY, C64, AMIGA, MEGA | 20 | The chip that plays it |
| FRAME | 60 HZ, 50 HZ | 21 | 50: slower arps, as on PAL |
| GLITCH | 0 to 100 % | 45 | A badly seated cartridge |

**LEAD**

| Setting | Values | CC | |
| - | - | - | - |
| SOUND | the console's own four (below) | 26 | The console's leads, 1 to 4 |
| LEVEL | 0 to 100 % | 27 | How loud the lead is |
| OCTAVE | -2 to 2 | 28 | An octave up or down |
| LENGTH | 10 to 100 % | 29 | Short notes or joined ones |
| VIBRATO | 0 to 100 % | 30 | Long notes wobble |
| ECHO | OFF, SHORT, MID, LONG | 31 | The lead again, later |
| SLIDE | 0 to 100 % | 32 | How long a glide takes |

**CHORDS**

| Setting | Values | CC | |
| - | - | - | - |
| SOUND | the console's own four (below) | 33 | The console's chord sounds |
| LEVEL | 0 to 100 % | 34 | How loud the chords are |
| OCTAVE | -2 to 2 | 35 | An octave up or down |
| ARP | OFF, SLOW, MID, FAST | 36 | How fast a chord is broken |
| SHAPE | UP, DOWN, UP DOWN, OCTAVES, RANDOM | 37 | The order of its notes |
| RHYTHM | HELD, EIGHTHS, OFFBEAT, GALLOP, FOURS | 38 | Held, or stabs in a pattern |

**BASS**

| Setting | Values | CC | |
| - | - | - | - |
| SOUND | the console's own four (below) | 39 | The console's basses, 1 to 4 |
| LEVEL | 0 to 100 % | 40 | How loud the bass is |
| OCTAVE | -2 to 2 | 41 | An octave up or down |
| LENGTH | 10 to 100 % | 42 | Short notes or joined ones |

**DRUMS**

| Setting | Values | CC | |
| - | - | - | - |
| KIT | the console's own four (below) | 43 | The console's drum kits |
| LEVEL | 0 to 100 % | 44 | How loud the drums are |

**BAR**

| Setting | Values | CC | |
| - | - | - | - |
| DICE | press to do it |  | An idea for this row |
| COPY | press to do it |  | Remember this bar |
| PASTE | press to do it |  | Put the copied bar here |
| CLEAR | press to do it |  | Empty this bar |

**HANDS**

| Setting | Values | CC | |
| - | - | - | - |
| TILT | OFF, ON | 50 | Tip: glitch. roll: bend |
| BEND | 1 to 12 st | 51 | How far a roll bends |
| STICK | OFF, MUTES | 52 | A push mutes a part |

**LINK**

| Setting | Values | CC | |
| - | - | - | - |
| LINK | OFF, ON |  | Sync with boards nearby |
| GROUP | 1 to 8 |  | Same group: they sync |
| RANGE | NEAR, ROOM, FAR |  | How far the radio reaches |
| WHO IS THERE | press to do it |  | The boards linked now |

<!-- /tables -->

## With other boards

LINK joins the flock: boards with the same GROUP share one beat over the radio. PIXEL's tempo is the song's. Next
to a board that keeps a loop the loop's tempo wins, and the song plays at that tempo without its own setting
changing. Alone, a start is at once. With other boards a start waits for the next bar line. The key and scale of
another board are followed until a song is loaded or KEY or SCALE is changed here. The chord that sounds is sent to
boards that follow chords.

PIXEL's four extra scales are its own. On the link PENTA MAJOR and ALL NOTES stand as MAJOR, PENTA MINOR and BLUES
as MINOR.

## MIDI

- Notes in play the lead: the newest key, until it is let go.
- Each setting is on the CC in the tables, both ways.
- Program change picks a song: the factory songs first, then yours.
- CC 64 (a sustain pedal) is REC.

## Limits

- A bar is sixteen steps: no 3/4.
- Eight songs of your own.
- The ATARI is out of tune by design: its chip has 32 pitches for each sound.
- One part's notes cannot overlap: a longer note stops where the next one starts.
