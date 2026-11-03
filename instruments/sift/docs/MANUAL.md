# SIFT manual

Version 0.2. Checked in the host simulator (`tools/sim`) and running on a board since 2026-10-22 (CPU 17 %, 19 % at
CHAOS 100 with FEEDBACK 100). The levels and the feel of the tilt are still to be tried in the hand.

## What it does with what it hears

1. Everything REC records goes into one memory, the corpus: 20 seconds, as long as a Samplooler take.
2. The corpus is cut into units of 32 ms (1024 samples). As each unit is finished, SIFT measures it:
   - **LOUD**: its level in dB.
   - **BRIGHT**: how fast it wiggles on average, as a frequency. A low hum is dark, a hiss is bright.
   - **pitch**: the note it plays, if it is periodic enough (62 Hz to 2 kHz).
   - **NOISE**: how far from periodic it is. A sung vowel is near 0, a breath or a cymbal near 1.
   - **TONE**: its pitch when it has one, else its brightness. The note it sounds like.
   - **TIME**: the order it was heard in.
3. A take is evened out when it ends: its loudest moment is set to half scale. Whatever is more than 42 dB under
   that is silence.
4. SIFT marks where syllables start (the cuts). Daisy Bell and the Amen know their own: each syllable's first
   consonant, each drum hit. For anything you record, a cut is where the level jumps 5 dB, where a silence ends, or
   where the pitch moves more than a semitone and a half.

## DECOMPOSE (the default mode)

A head moves through the recording, from the oldest sound to the newest and round again. Grains are taken where it
is. GRAINS > CHAOS (the knob) decides how:

| CHAOS | | |
| - | - | - |
| 0 | WHOLE | Grains of 120 ms every 60 ms, each from where the head is, under a smooth window. At speed 1 they add up to the recording exactly: it plays as it was recorded. |
| 2 to 15 | WEARING | Now and then a grain is said twice (a skip) or left out (a hole), and the pitch wavers slowly, like old tape. |
| 10 to 50 | SYLLABLES | The stream stays whole, but each time the head reaches a syllable it may be damaged: from 1 in 10 at 15 to nearly all at 50. Damaged how: stammered (its start said 2 to 5 times while the line waits), swapped with a neighbour one or two syllables away, dropped (a hole as long as it), played backwards, or moved up or down a few semitones. |
| 45 to 75 | PHONEMES | The grains shrink from 120 to 20 ms. They come from near the head, then from wherever in the recording sounds like what the head is on now (the map's neighbours). Pitches scatter, some play backwards, holes open, the edges harden. |
| 75 to 100 | DUST | 20 down to 4 ms from anywhere in the recording, at DENSITY, up to two octaves either way, half backwards, snapped on and off. |

What damages a syllable is weighed by DECOMPOSE > STUTTER (stammers), JUMPS (swaps), GAPS (drops), CHAOS > REVERSE
(backwards) and CHAOS > SCATTER (out of tune). JUMPS also sets how often the phonemes jump; GAPS how many holes;
STUTTER how often a grain is said again. DECOMPOSE > BITE is how violent the far end gets: it adds drive from the
middle up and crush near the end.

The stage you are in is written in the bottom left corner: WHOLE, WEARING, SYLLABLES, PHONEMES, DUST.

### How far the words go

whisper.cpp (a speech recogniser, run on a computer) listened to 18 seconds of Daisy at each setting and counted the
16 words of the two lines, in order:

| CHAOS | 0 | 10 | 20 | 30 | 40 | 50 | 60 |
| - | - | - | - | - | - | - | - |
| words heard | 15 | 15 | 10 | 11 | 8 | 3 | 0 |

What it wrote: at 20, "I'm F-crazy, all for the love of you. Daisy, da-da-daisy, give me all answers." At 30, "Give
me your answer, doo-doo-doo-doo." At 50, "crazy, crazy, crazy, crazy, I love you." At 60 it heard singing and no
words. `SIFT_WHISPER=0,20,40,60 tools/sim/run.sh inst` measures it again (it needs whisper.cpp and ffmpeg).

### The tilt

- **Roll** (tilt left or right): pushes CHAOS. Right edge down, it comes apart: at 30 degrees (TILT RANGE) it is dust
  whatever the knob says. Left edge down, it comes back together. Level, it is where the knob is. The first 4
  degrees do nothing, so a hand that is nearly level holds still. The header's gauge shows the knob as a solid mark
  and where the tilt has pushed it as a hollow one.
- **Pitch** (tip it towards you or away): the head's speed. Towards you it slows, stops half way (FROZEN: the grains
  keep coming from one moment), and at the end runs backwards at normal speed. Away from you it runs up to twice as
  fast. DECOMPOSE > SPEED sets the speed when the box is level (100 % is as recorded; 0 frozen; negative backwards).
- **Hold the knob**: the tilt stays where it is, so you can put the box down in pieces.

### DIRT

- **DRIVE**: distortion. **CRUSH**: fewer bits and a lower sample rate. Both on top of what BITE adds.
- **FEEDBACK**: the sound goes into a delay (FB TIME, 20 ms to 1 s) and back into itself, through the drive and the
  crush. Short times ring like a metal pipe; long ones repeat. At 100 it screams and holds itself.
- **ROT**: what comes out is written back into the recording where the head reads. At CHAOS 0 that is the recording
  itself, with the space and the tone on it, which build up pass after pass (Alvin Lucier's room). Torn apart, the
  tearing stays in it: the next pass is torn again. The units the head leaves are measured again, so the map follows.
  Choosing a Daisy or Amen sound again brings the recording back whole; your own recording does not come back unless
  you saved it.

## The material

TAKE > BANK and TAKE > LOAD:

- **DAISY**: the first two lines of Daisy Bell (Harry Dacre, 1892, public domain), "Daisy, Daisy, give me your answer,
  do! I'm half crazy, all for the love of you!", sung by MANTRA's formant voice the way the IBM 7094 sang it in 1961,
  a man's octave, a quarter note of 0.36 s (where whisper.cpp heard the most words in MANTRA's tests). It is sung on
  the board when it is loaded, in about a second, not stored.
- **AMEN**: the Amen break's pattern on WRECK's synthesized kit, printed through a room onto tape: four takes, each a
  little different, about 14 seconds. It takes a few seconds to print. No one's recording is in the firmware; load the
  real one from a Samplooler slot if you have it.
- **STARTER**: the first version's sounds (plucks up three octaves, bells, a buzzing voice, breaths, clicks).
- **SIFT 1 to 4**: what you saved. **SLOT 1 to 8**: the Samplooler's saved takes (read only).

At power-on SIFT loads whatever BANK points to (Daisy until you save something). A sound that brings its material
(the DAISY and AMEN sounds) loads it when you choose it, but never over your own recording: that stays until you load
something else.

## REC

- **Hold REC**: record while you hold it. **Tap REC** (under 0.3 s): LIVE, it keeps recording until you tap again.
- **TAKE > TAKES ADD** (the default): each take goes after the last one; when the 20 s are full the oldest goes first.
  LIVE with ADD is a window on the last 20 seconds of the room. **TAKES NEW**: each take starts the corpus over.
- **TAKE > RECORD**: MIC, USB (the computer's sound), ITSELF (SIFT's own output), MIC+ITSELF.
- **TAKE > SAVE** writes every unit into a SIFT bank, in the order they were heard (the sound is muted while it
  writes). **CLEAR**: nothing.

## MAP (the first version)

GRAINS > MODE MAP. Every unit is a dot on a map, placed by two of its measures (MAP > ACROSS, UP: TONE and NOISE by
default). Each axis is half the measure's range and half its rank, so the dots fill the map. The tilt is a cursor
over the map (MAP > CURSOR: TILT, VOICE or STICK); the grains come from the dots near it.

**ORDER** (CHAOS 0): the dots nearest the cursor, ORDER > NOTES of them, sorted by PATTERN (UP, DOWN, BOUNCE, AS
PLAYED, SHUFFLE), one on every step of ORDER > GRID on the flock's beat, STACK at once, each pitched one tuned to the
key (TUNE TO). **CHAOS** (100): grains at random times, DENSITY a second, from inside the dashed ring (SPREAD), thrown
in pitch (SCATTER), some backwards (REVERSE). In between, the grid thins out as the swarm thickens. GRAINS > SIZE and
SHAPE set the grains.

**VOICE**: the mic moves the cursor to where your sound would sit on the map, the cloud's level follows your voice,
and with SING ALONG the pitched grains take your note. Use headphones or the line out (SYSTEM > SPEAKER OFF): it
hears the speaker too. A MIDI note held does the same as SING ALONG.

## The shake

A jolt over half a g, or a fast spin, throws it apart and scatters a burst of short grains; it settles in a second or
two. CHAOS > SHAKE is how much (0: shaking does nothing).

## SPACE and SONG

TONE darkens or thins it; SPACE is the room after it, TAIL how long the room rings. SONG > TEMPO, KEY and SCALE are
shared with the boards on the link (LINK on, the same GROUP); WHO IS THERE lists them.

## MIDI (USB)

| Out | |
| - | - |
| notes | MAP: each pitched grain on the order side, at the note it plays |
| CC 1 / 2 / 3 | the cursor across / the cursor up / how far apart it is now (the knob, the tilt and the shake) |
| CC 20 and up | every setting, as you change it |
| program change | the sound |

| In | |
| - | - |
| a note held (not 60 or 64) | MAP: the order side's pitched grains play that note |
| note 60, CC 64 | REC |
| note 64 | hold the tilt |
| CC 20 and up, program change | settings, sounds |

## Settings

<!-- tables -->
### GRAINS

| Setting | CC | |
| - | - | - |
| MODE | 20 | It falls apart, or a map: DECOMPOSE, MAP |
| CHAOS | 21 | 0 whole, 100 in pieces (0..100 %) |
| SIZE | 22 | How long each grain is (0.005..0.8 s) |
| SHAPE | 23 | In map: each grain's shape: SOFT, PLUCK, SWELL, SLICE, HARD |
| PITCH | 24 | Everything up or down (-24..24 st) |
| DRIFT | 25 | Cents each grain wanders (0..100) |
| LEVEL | 26 | How loud (0..100 %) |

### DECOMPOSE

| Setting | CC | |
| - | - | - |
| SPEED | 27 | How fast it moves on (-100..200 %) |
| STUTTER | 28 | Syllables caught, repeated (0..100 %) |
| JUMPS | 29 | How far pieces come from (0..100 %) |
| GAPS | 30 | Holes in it (0..100 %) |
| BITE | 31 | How violent the end gets (0..100 %) |

### DIRT

| Setting | CC | |
| - | - | - |
| DRIVE | 32 | Distortion (0..100 %) |
| CRUSH | 33 | Fewer bits, a lower rate (0..100 %) |
| FEEDBACK | 34 | Its sound back into itself (0..100 %) |
| FB TIME | 35 | How long the loop is (0.02..1.0 s) |
| ROT | 36 | Damage kept in the sound (0..100 %) |

### ORDER

| Setting | CC | |
| - | - | - |
| GRID | 37 | A grain on every step of it: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| NOTES | 38 | Grains in the pattern (1..16) |
| PATTERN | 39 | The order they play in: UP, DOWN, BOUNCE, AS PLAYED, SHUFFLE |
| STACK | 40 | Grains at once: a chord (1..4) |
| TUNE TO | 41 | Notes the grains land on: OFF, ALL NOTES, SCALE, PENTATONIC |

### CHAOS

| Setting | CC | |
| - | - | - |
| DENSITY | 42 | Grains a second in chaos (2..200 /s) |
| SPREAD | 43 | How far the cursor reaches (0..100 %) |
| SCATTER | 44 | Pitches thrown about (0..24 st) |
| REVERSE | 45 | Grains played backwards (0..100 %) |
| SHAKE | 46 | Shake it: how wild it gets (0..100 %) |

### MAP

| Setting | CC | |
| - | - | - |
| CURSOR | 47 | What moves the cursor: TILT, VOICE, STICK |
| ACROSS | 48 | Left to right on the map: TIME, TONE, BRIGHT, LOUD, NOISE |
| UP | 49 | Bottom to top on the map: TIME, TONE, BRIGHT, LOUD, NOISE |
| TILT RANGE | 50 | Degrees to reach an edge (10..60) |
| SING ALONG | 51 | Grains take your note: OFF, ON |

### TAKE

| Setting | CC | |
| - | - | - |
| RECORD | 52 | What rec records: MIC, USB, ITSELF, MIC+ITSELF |
| TAKES | 53 | Add on, or start over: ADD, NEW |
| BANK |  | What load and save use: DAISY, AMEN, STARTER, SIFT 1, SIFT 2, SIFT 3, SIFT 4, SLOT 1, SLOT 2, SLOT 3, SLOT 4, SLOT 5, SLOT 6, SLOT 7, SLOT 8 |
| LOAD |  | The bank's sound, sifted |
| SAVE |  | Every grain into the bank |
| CLEAR |  | No grains: record new ones |

### SPACE

| Setting | CC | |
| - | - | - |
| TONE | 58 | Down: dark  up: thin (-100..100 %) |
| SPACE | 59 | How much room round it (0..100 %) |
| TAIL | 60 | A small room to endless (0..100 %) |

### SONG

| Setting | CC | |
| - | - | - |
| TEMPO | 61 | Shared with linked boards (50..200) |
| KEY | 62 | The grains are in this key: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE | 63 | Its notes: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

## Not done yet

- On the board the decomposition costs 17 to 19 % of the CPU; MAP's busiest case (32 long grains, the dirt and the
  loop) has not been measured there. The tilt's feel and the levels need playing.
- Printing the Amen takes several seconds on the board (WRECK's measured 3 s a take); the screen shows how far.
- The VOICE cursor's guard against hearing its own speaker is a rough rule, untested with a real mic.
- A save mutes the sound for its whole length.
