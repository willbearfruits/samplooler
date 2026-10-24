# DWELL manual

Version 0.1. Checked in the host simulator (`tools/sim`, 156 checks). Not on a board yet.

## The lattice

Every tone is a ratio of whole numbers on one note, 1/1 (SONG > KEY at LATTICE > OCTAVE: C3, 130.8 Hz, by default).
A ratio is 3^b times p^c brought into one octave, where p is the prime LATTICE > LIMIT names:

| LIMIT | the roll's axis | the corners |
| - | - | - |
| 7 (YOUNG) | 7/4, 49/32 up; 8/7, 64/49 down | 21/16, 7/6, 63/32, 32/21 ... La Monte Young's Dream House avoids the 5 |
| 5 | 5/4, 25/16 up; 8/5, 32/25 down | 15/8, 5/3, 45/32 ... the classical lattice |
| 3 | none: the roll does nothing | only the fifths: Pythagorean tuning |

The tilt's axis is the fifths: tip the box away from you one step (LATTICE > TILT STEP degrees, 10 by default) and
the lead is 3/2; two steps, 9/8; three, 27/16; towards you 4/3, 16/9, 32/27. Roll right for 7/4, right again for
49/32; left for 8/7. A cell counts once the box has been on it for LATTICE > DWELL (0.4 s), so cells you cross on the
way do not sound. The lead's own cell is sticky: it takes 0.7 of a step to leave it.

The screen prints the lead's ratio, its cents above 1/1 and its distance from the nearest equal-tempered note (7/4
is 31 cents under the piano's minor seventh). MIDI out sends each new lead as a note with the pitch bend that makes
it just, for a synth with a two-semitone bend range.

## The tones

DRONE > VOICES tones stay with you: the lead (the cell you are on) and the trail, the cells you were on before, oldest
first to go. Walk back onto a cell in the trail and that tone is the lead again. DRONE > ROOT adds 1/1 an octave
under them all (not counted in VOICES; with ROOT on, VOICES is at most 5).

DRONE > DRIFT (0.5 s to 5 minutes, 20 s by default) is how long any change takes: a new tone swelling in, an old one
fading, the fundamental moving. DRONE > MOVE says how a new cell arrives: SWELL (the new tone swells in beside the old
ones; when all VOICES are taken the oldest fades out first and comes back on the new cell: so a move takes up to two
DRIFTs then) or GLIDE (the lead itself slides to the new cell, and the cell it leaves is taken over by the oldest of
the trail, sliding there: nothing ever starts or stops, as in Radigue's Adnos).

## DENSE: the knob

DRONE > DENSE is the knob in play. At 0 every tone is one sine at its exact ratio and nothing beats: the Dream House.
Turning up, each tone becomes a cluster of copies (CLUSTER > COPIES at the top, fewer on the way), their frequencies
CLUSTER > BEAT Hz apart, so the cluster beats at that rate and its multiples; the harmonics come in (CLUSTER > BRIGHT
times DENSE for sines; BRIGHT as it is for the other sources) and the level rises about 6 dB. CLUSTER > BEAT SYNC:
TEMPO makes the beat rate the flock's tempo (SONG > TEMPO, or a linked board's: 120 BPM is 2 Hz), so the drone pulses
with a BAND without any envelope. CLUSTER > DIFF TONE adds a sub sine at the difference of the two lowest tones when
that is 20 Hz or more (Tartini's tone, which your ear makes anyway).

## Sources

| SOURCE | | its settings |
| - | - | - |
| SINE | partials rolled off by BRIGHT | |
| REED | a harmonium reed: every harmonic with a formant bump | BODY |
| BOW | a bowed string with a little bow noise and a wooden body (Tony Conrad's violin) | BODY |
| TANPURA | one string per tone, plucked in turn, with the jawari: the curved bridge's buzz. A plain string (JAWARI 0) darkens after the pluck; with the jawari its overtones come up over the first half second, then fade with the string (measured: the brightness rises from the 4th harmonic to the 5.6th, where a plain string falls to the 2nd) | PLUCK (how often), JAWARI |
| SHRUTI | a shruti box: the reed with the bellows' slow swell and a little air | BREATH |
| GURDY | a hurdy-gurdy: the wheel's rumble, the trompette's buzz at a strum (and on every turn when dense) | WHEEL, BODY |
| WAVE | one period of the tone you sang (REC held), played at every ratio | |
| ROOM | the room's own resonances (ROOM PASS), played at every ratio | |

Hold REC, sing one steady note for a second or two, let go. DWELL finds its period and keeps one cycle of it; the
source turns to WAVE and tells you the Hz. If nothing steady was heard it says so and keeps the last table (a built-in
voice-like one at first).

## ROOM PASS

ROOM > ROOM PASS is Alvin Lucier's "I Am Sitting in a Room" with the drone as the voice. Pass 1: the drone plays as it
is while the mic records the room for ROOM > PASS LEN seconds. Every pass after: the drone is silent, the last
recording plays through the speaker, the mic records that. After ROOM > PASSES passes (12 by default: about a minute),
the last recording is the ROOM source, looped and transposed to every ratio of the lattice. What the room does not
resonate at has gone; what it does has taken over. A tap on REC stops it early. It needs the speaker on (SYSTEM >
SPEAKER) and a room; with headphones the mic hears nothing and it stops after the first pass. Move the box between
passes and the room answers differently.

## The strum

A knock on the box strums every tone: the level jumps and decays over STRUM > STRUM seconds, the brightness opens with
it; TANPURA plucks all its strings and starts its cycle of plucks again, GURDY buzzes. STRUM > SENSE sets how hard a knock has to be; STRUM > KNOCK OFF
leaves the box deaf to knocks.

## REC tapped

A tap on REC rests the drone: everything fades over two seconds. Another tap brings it back. While a room pass runs
the tap stops the pass instead.

## The stick

Up and down shift the lead an octave. Left makes the chord louder than the lead, right the lead louder than the
chord. On a board without a tilt sensor the stick walks the lattice instead.

## The link

Boards within radio reach that run DWELL, BAND, SING, the Samplooler or any other instrument on the link share a
tempo and a key (SONG > TEMPO, KEY, SCALE; LINK, GROUP, RANGE as everywhere). SONG > FOLLOW: when a linked board plays
a chord live, 1/1 moves to the lattice's nearest ratio for that chord's root (the fifth degree gives 3/2), at DRIFT,
and back when the chord is let go. SONG > SPREAD: with another DWELL in the room, this board sounds only one tone of
its own chord, chosen by its place among the DWELLs heard (the first board the root or the lead, the second the next
tone, and so on), so a few boards are one chord in the room: the Theatre of Eternal Music, one tone each. SONG > WHO
IS THERE lists the boards heard.

## Sounds

| | |
| - | - |
| DREAM HOUSE | pure sines, 7-limit, four voices and the root |
| NIBLOCK | a bowed wall: DENSE 80, eight copies beating at 0.3 Hz, no root |
| ADNOS | a reed that slides: GLIDE, DRIFT 90 s |
| IN A ROOM | the ROOM source, 12 passes of 4 s set: run ROOM > ROOM PASS |
| TANPURA | four strings plucked every 0.8 s, the jawari at 55 |
| SHRUTI BOX | the reed with the bellows |
| HURDY GURDY | the wheel at 1.2 Hz, short strums |
| STRUMMING | sines, six voices, strums ringing 5 s (Charlemagne Palestine) |
| CONRAD | a bow in 5-limit, four copies at 0.8 Hz |
| DAVACHI | six reed voices that slide over 45 s |
| DEEP LISTENING | a quiet reed in a long hall |
| TEMPO BEATS | the clusters beat at the flock's tempo |
| YOUR TONE | the WAVE source: hold REC and sing |
| TARTINI | two sines and their difference tone |
| PYTHAGORAS | 3-limit: the fifths only |

## Settings

Every setting is on MIDI CC 20 + its number (the table says which) both ways; program change picks a sound.

<!-- tables -->
### DRONE

| Setting | CC | |
| - | - | - |
| DENSE | 20 | 0 pure sines, 100 a wall (0..100 %) |
| VOICES | 21 | Tones that stay with you (1..6) |
| ROOT | 22 | 1/1 an octave under all: OFF, ON |
| DRIFT | 23 | How long a change takes (0.5..300.0 s) |
| MOVE | 24 | New tones swell, or slide: SWELL, GLIDE |
| LEVEL | 25 | How loud (0..100 %) |

### LATTICE

| Setting | CC | |
| - | - | - |
| LIMIT | 26 | The primes the ratios use: 7 (YOUNG), 5, 3 |
| OCTAVE | 27 | Where 1/1 sits (1..4) |
| TILT STEP | 28 | Degrees per cell (6..20) |
| DWELL | 29 | Stay on a cell this long (0.1..2.0 s) |

### CLUSTER

| Setting | CC | |
| - | - | - |
| COPIES | 30 | Copies per tone at dense (1..8) |
| BEAT | 31 | How fast the copies beat (0.05..8.0 Hz) |
| BEAT SYNC | 32 | Beats at the flock's tempo: OFF, TEMPO |
| BRIGHT | 33 | Harmonics (0..100 %) |
| DIFF TONE | 34 | A sub at the difference (0..100 %) |

### SOURCE

| Setting | CC | |
| - | - | - |
| SOURCE | 35 | What each tone is: SINE, REED, BOW, TANPURA, SHRUTI, GURDY, WAVE, ROOM |
| PLUCK | 36 | Tanpura: plucks this often (0.2..4.0 s) |
| JAWARI | 37 | Tanpura: the bridge's buzz (0..100 %) |
| WHEEL | 38 | Gurdy: the wheel's turn (0.3..4.0 Hz) |
| BREATH | 39 | Shruti: the bellows (0..100 %) |
| BODY | 40 | Bow, reed: its wooden box (0..100 %) |

### STRUM

| Setting | CC | |
| - | - | - |
| KNOCK | 41 | A knock strums every tone: OFF, ON |
| STRUM | 42 | How long a strum rings (0.2..8.0 s) |
| SENSE | 43 | How easily a knock counts (0..100 %) |

### ROOM

| Setting | CC | |
| - | - | - |
| PASSES | 44 | Times through the room (1..30) |
| PASS LEN | 45 | Seconds per pass (2..ROOM_MAX_S s) |
| ROOM PASS |  | Play, record, again, again |

### SPACE

| Setting | CC | |
| - | - | - |
| SPACE | 47 | How much hall (0..100 %) |
| TAIL | 48 | A small room to endless (0..100 %) |

### SONG

| Setting | CC | |
| - | - | - |
| TEMPO | 49 | Shared with linked boards (50..200) |
| KEY | 50 | 1/1 is this note: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE | 51 | For chords from the link: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| FOLLOW | 52 | A linked chord moves 1/1: OFF, ON |
| SPREAD | 53 | Each board one tone: OFF, ON |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

## MIDI

- In: CC 20.. sets a setting, program change picks a sound, note 60 (or CC 64) is REC, note 64 holds the motion,
  any other note between C1 and B4 moves 1/1 to that note (KEY and OCTAVE follow).
- Out: each setting as it changes, program change on a sound change, the lead as a note with a pitch bend (two
  semitones of range: the bend makes the note just), CC 12/13 the lean and CC 14/15 the stick when SYSTEM > MIDI CC
  OUT is on.

## Levels

Nominal peaks around -12 dBFS with four voices; the wall at DENSE 100 about 6 dB louder; the core's limiter at -1
dBFS after everything. The root at OCTAVE 3 is 65 Hz: the small speaker loses it, the line out has it.
