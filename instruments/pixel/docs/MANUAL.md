# PIXEL manual

PIXEL is a chiptune machine. A song is four parts written on a grid of sixteenth steps: DRUMS, BASS, CHORDS and
LEAD. Bars are chained into a song. One of eight old sound chips plays it, and the knob changes which.

It has not run on a board yet: everything here was checked in the simulator.

## Playing

| | |
| - | - |
| tap REC | play the song from the start; tap again to stop |
| turn the knob | the console: BEEPER, ATARI, AY, NES, GAME BOY, C64, AMIGA, MEGA |
| press the knob | the menu |
| press the knob twice | the knob turns the volume, until it rests 3 s or you press it again |
| hold the knob | the editor (below); hold again to come back |
| stick up / right / down / left | mute or bring back the LEAD / CHORDS / BASS / DRUMS |
| tip the far edge down | the cartridge slips: the tune glitches, more the further you tip |
| roll left / right | everything bends down / up (HANDS > BEND sets how far) |

The screen draws the song as a level that scrolls by. The lead's notes are platforms and a runner hops along them.
The bass is blocks on the ground. A kick shakes the ground, a snare throws sparks, hats twinkle as stars. The chord's
name is in the top left corner. On the right are the chip's own voices, each with the wave it makes now, and four
lamps for the parts: L, C, B, D. Along the bottom of the header are the bar's sixteen steps.

A new console starts on the next driver tick after a 3 ms fade, so the knob can be turned while the song plays.

BOOT is a second REC button on every board.

## Writing

Hold the knob. One bar is on the screen, sixteen steps across, with a mark every four. There are five rows; the
stick's up and down moves between them: SONG, LEAD, CHORDS, BASS, DRUMS. The header shows the row, the pattern's
letter, which bar of the song this is, and what is under the cursor.

| | LEAD, BASS | CHORDS | DRUMS | SONG |
| - | - | - | - | - |
| stick left / right | the cursor along the steps, on into the next bar | the same | the same | from bar to bar |
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

Without a stick the knob walks the cursor step by step and on into the next row, and press and turn changes what a
turn would have.

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
The eight chips and their sounds are described here once they are in.
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
