# SIFT manual

SIFT runs on a board (flashed 2026-10-22: CPU 16 %, 150 KB of internal RAM free, the 20 s corpus in PSRAM) but has
not been played much yet. The rest was checked in the host simulator (`tools/sim`); the levels, the feel of the tilt
and VOICE with a real mic are still to be tried.

## What it does with what it hears

1. Everything REC records goes into one memory, the corpus: 20 seconds, as long as a Samplooler take.
2. The corpus is cut into units of 32 ms (1024 samples). As each unit is finished, SIFT measures it:
   - **LOUD**: its level in dB.
   - **BRIGHT**: how fast it wiggles on average, as a frequency. A low hum is dark, a hiss is bright.
   - **pitch**: the note it plays, if it is periodic enough (from 62 Hz to 2 kHz).
   - **NOISE**: how far from periodic it is. A sung vowel is near 0, a breath or a cymbal near 1.
   - **TONE**: its pitch when it has one, else its brightness. The note it sounds like.
   - **TIME**: the order it was heard in.
3. A take is evened out when it ends: its loudest moment is set to half scale. Whatever is more than 42 dB under
   that is silence. Silence is not on the map and never plays.
4. Each unit that is not silence is a dot on the map. Its attack (the first moment over half its peak) is where
   grains from it start on the order side.

The starter sounds SIFT boots with are made on the board, not recorded: plucked strings up three octaves, bells,
a buzzing voice singing AH, EE and OO, breaths from dark to bright, thumps, clicks and a shaker. They fill the map
from corner to corner. TAKE > STARTER brings them back.

## The map

MAP > ACROSS and MAP > UP pick two of the measures (TIME, TONE, BRIGHT, LOUD, NOISE). The default is TONE across
(low on the left, high on the right) and NOISE up (clean at the bottom, noisy at the top). Each axis is half the
measure's range and half its rank among the dots. The range keeps similar sounds together. The rank spreads the
dots over the whole map, so there are grains wherever the cursor goes, and the middle of the map is the middle of
your material. The names of the axes show for a few seconds after power-on and after you change them.

Try TIME across and LOUD up (the TAPE sound): tilting left and right is moving through the recording, like a tape.

## The cursor

- **TILT** (the default): the box is a tray and the cursor rolls to the low side. Right edge down, the cursor goes
  right. Bottom edge down, it goes down. MAP > TILT RANGE is how far you tilt to reach an edge (30 degrees).
- **VOICE**: the mic moves it. SIFT measures what you sing or play, every 30 ms, and the cursor goes to where that
  sound would sit on the map. The cloud's level follows your voice: silent when you are. With MAP > SING ALONG
  on, every pitched grain is tuned to your note (within an octave of where it is). It also hears the speaker, so
  play it through headphones or the line out, with SYSTEM > SPEAKER OFF.
- **STICK**: the joystick pushes the cursor around; it stays where you leave it.

Hold the knob to hold the cursor where it is (a padlock in the header). Hold it again to let go.

## From ORDER to CHAOS

The knob turns CHAOS, from 0 to 100. Everything below changes with it, smoothly.

**The order side** (CHAOS 0): the dots nearest the cursor make a pattern. ORDER > NOTES is how many (6). PATTERN
sorts them: UP (low to high), DOWN, BOUNCE (up and back), AS PLAYED (the order you recorded them in), SHUFFLE (a
fixed shuffle: the same dots always give the same order). One grain plays on every step of ORDER > GRID (1/16) of
the beat, the flock's beat if a board is linked. The pattern's place is counted from the beat, so it stays in
step with the bar. STACK plays 2 to 4 grains at once, the next ones in the pattern: chords. TUNE TO moves every
pitched grain to the nearest note of the key (SCALE, PENTATONIC or ALL NOTES; OFF leaves them as they are).
Unpitched grains are not tuned. The pattern is drawn on the screen as lines between its dots, and each grain
lights up as it plays.

**The chaos side** (CHAOS 100): grains come at random times, CHAOS > DENSITY a second on average. They come from
the dots inside the dashed ring around the cursor, nearer ones more often; SPREAD is how wide the ring is. Each
grain may start a little way from its dot's attack, may be thrown up or down in pitch (SCATTER, in semitones),
plays backwards with the chance of REVERSE, and is a little longer or shorter than SIZE. Tuning is off.

**In between**: each step of the grid plays with a chance of 1 minus CHAOS squared, and drifts late by up to half
a step as CHAOS rises. The random grains come in at DENSITY times CHAOS to the power 1.5. The tuning fades from
the key to free. The ring opens. On the screen the dots let go of their lattice and start to circle.

**The grains**: GRAINS > SIZE is how long each one is (5 ms to 0.8 s). SHAPE is its envelope: SOFT (a smooth
swell and fade), PLUCK (a fast start and a fall), SWELL (a slow start and a fast end), SLICE (flat, with short
fades: the most of the sound). PITCH moves everything up or down. DRIFT gives each grain a few cents of its own.
When many grains overlap, each one is quieter, so the level stays even.

## The shake

A jolt over half a g, or a fast spin, throws SIFT towards chaos and scatters a burst of grains. It settles in a
second or two. CHAOS > SHAKE is how much (0: shaking does nothing). The dots fly apart on the screen and spring back.

## REC and the corpus

- **Hold REC**: record while you hold it. Let go and it stops. The new grains fall onto the map as they are heard.
- **Tap REC** (under 0.3 s): LIVE. It keeps recording until you tap again.
- **TAKE > TAKES ADD** (the default): each take goes after the last one. When the corpus is full, the oldest
  grains are written over first. LIVE with ADD is a window on the last 20 seconds of the room.
- **TAKE > TAKES NEW**: each take starts the corpus over. The old grains fade out when you press REC. A take
  stops by itself when the 20 seconds are full.
- **TAKE > RECORD**: MIC, USB (what the computer plays into the board's sound card), ITSELF (SIFT's own sound:
  resampling, round and round with LIVE), or MIC+ITSELF.
- Grains never play from the stretch that is being written, so recording while it plays does not click.
- **TAKE > CLEAR**: no grains at all. **TAKE > STARTER**: the starter sounds.

## Banks

TAKE > BANK picks where LOAD and SAVE go. SIFT 1 to 4 are SIFT's own. SLOT 1 to 8 are the Samplooler's saved
slots: SIFT loads them but never writes them. SAVE writes every grain into the bank, in the order they were heard,
each take at its own level (the sound is muted while it writes, a few seconds for 20 s of sound). LOAD reads a bank
and sifts it again. At power-on SIFT loads the bank that is chosen, if it is one of its own and has something in it.
The files are mono 16-bit WAVs on the same flash partition as the Samplooler's slots (`sift1.wav` to `sift4.wav`),
so flashing one firmware or the other keeps both.

## SPACE

TONE darkens (down) or thins (up) everything. SPACE is how much goes into a room after the grains; TAIL is how long
the room rings, from a small room to about 20 seconds.

## SONG and the link

SONG > TEMPO, KEY and SCALE are shared with the boards on the link (LINK on, the same GROUP). A board that changes
them changes them here, and the other way round. The scales are numbered as BAND's, JAM's and SING's: MAJOR,
MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR. Alone, SIFT keeps its own beat at TEMPO. SONG > WHO IS THERE lists
the boards it hears.

## MIDI (USB)

| Out | |
| - | - |
| notes | each pitched grain on the order side, at the note it plays, for SIZE |
| CC 1 / 2 / 3 | the cursor across / the cursor up / how chaotic it is now (the knob plus the shake) |
| CC 20 and up | every setting, as you change it |
| program change | the sound |

| In | |
| - | - |
| a note held (not 60 or 64) | the order side's pitched grains play that note |
| note 60, CC 64 | REC |
| note 64 | hold the cursor |
| CC 20 and up, program change | settings, sounds |

## Settings

<!-- tables -->
### GRAINS

| Setting | CC | |
| - | - | - |
| CHAOS | 20 | Order at 0, chaos at 100 (0..100 %) |
| SIZE | 21 | How long each grain is (0.005..0.8 s) |
| SHAPE | 22 | Each grain's rise and fall: SOFT, PLUCK, SWELL, SLICE |
| PITCH | 23 | Everything up or down (-24..24 st) |
| DRIFT | 24 | Cents each grain wanders (0..100) |
| LEVEL | 25 | How loud (0..100 %) |

### ORDER

| Setting | CC | |
| - | - | - |
| GRID | 26 | A grain on every step of it: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| NOTES | 27 | Grains in the pattern (1..16) |
| PATTERN | 28 | The order they play in: UP, DOWN, BOUNCE, AS PLAYED, SHUFFLE |
| STACK | 29 | Grains at once: a chord (1..4) |
| TUNE TO | 30 | Notes the grains land on: OFF, ALL NOTES, SCALE, PENTATONIC |

### CHAOS

| Setting | CC | |
| - | - | - |
| DENSITY | 31 | Grains a second in chaos (2..200 /s) |
| SPREAD | 32 | How far the cursor reaches (0..100 %) |
| SCATTER | 33 | Pitches thrown about (0..24 st) |
| REVERSE | 34 | Grains played backwards (0..100 %) |
| SHAKE | 35 | Shake it: how wild it gets (0..100 %) |

### MAP

| Setting | CC | |
| - | - | - |
| CURSOR | 36 | What moves the cursor: TILT, VOICE, STICK |
| ACROSS | 37 | Left to right on the map: TIME, TONE, BRIGHT, LOUD, NOISE |
| UP | 38 | Bottom to top on the map: TIME, TONE, BRIGHT, LOUD, NOISE |
| TILT RANGE | 39 | Degrees to reach an edge (10..60) |
| SING ALONG | 40 | Grains take your note: OFF, ON |

### TAKE

| Setting | CC | |
| - | - | - |
| RECORD | 41 | What rec records: MIC, USB, ITSELF, MIC+ITSELF |
| TAKES | 42 | Add on, or start over: ADD, NEW |
| BANK |  | Slot: a samplooler take: SIFT 1, SIFT 2, SIFT 3, SIFT 4, SLOT 1, SLOT 2, SLOT 3, SLOT 4, SLOT 5, SLOT 6, SLOT 7, SLOT 8 |
| LOAD |  | The bank's sound, sifted |
| SAVE |  | Every grain into the bank |
| CLEAR |  | No grains: record new ones |
| STARTER |  | The sounds it came with |

### SPACE

| Setting | CC | |
| - | - | - |
| TONE | 48 | Down: dark  up: thin (-100..100 %) |
| SPACE | 49 | How much room round it (0..100 %) |
| TAIL | 50 | A small room to endless (0..100 %) |

### SONG

| Setting | CC | |
| - | - | - |
| TEMPO | 51 | Shared with linked boards (50..200) |
| KEY | 52 | The grains are in this key: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE | 53 | Its notes: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

## Not done yet

- On a board the default sound takes 16 % of the CPU (25 % at most). The busiest case (32 grains at once, long
  and dense) has not been measured there yet. The tilt's feel and the levels need playing.
- The VOICE cursor's guard against hearing its own speaker is a rough rule (it ignores the mic when it is quieter
  than about a sixth of what SIFT plays). It is untested with a real mic.
- The pitch finder looks for 62 Hz to 2 kHz. Very low or very high notes are measured by their brightness.
- A save mutes the sound for its whole length.
