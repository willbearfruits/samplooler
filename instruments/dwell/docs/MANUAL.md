# DWELL manual

Version 0.3. Checked in the host simulator (`tools/sim`, 223 checks). 0.2 runs on a board (19 % CPU on DREAM HOUSE,
82 KB free, 44 KB with both radios); 0.3 has not been on a board yet.

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

## ENERGY: the crescendo

Movement pumps energy into the drone: the box turned faster than 25 degrees a second (a hand holding still is slower),
up to full at 375, and its jolts. While it moves, the energy climbs at full speed to the top in ENERGY > CLIMB seconds
(8 by default); while it is still, it falls back to nothing in ENERGY > FALL seconds (20). Gestures add up: two
swings with a pause between leave more than one. The energy:

- multiplies every voice: from ENERGY > REST LEVEL (-14 dB) at nothing to full at the top, even in dB;
- raises DENSE towards 100 % by TO DENSE (60 %), and the harmonics with it;
- raises the overdrive towards 100 % by TO DIRT (50 %).

BOW > HAND (its own page below) bows each voice by the movement itself instead. While the box swings the lattice does
not move: a cell counts only once the box is calm (under 60 degrees a second) for DWELL.

Measured in the simulator: four seconds of swinging with CLIMB 4 s and REST LEVEL -20 dB took the drone up by 19.4 dB;
five seconds still with FALL 10 s, half way back down; ten, at rest again.

## SHAKE

A shake is jolt after jolt: the acceleration changing by more than 0.35 g from one hundredth of a second to the next,
for a while (it takes about 0.15 s to count fully, so one knock does not; it dies away in about 0.7 s). ENERGY > SHAKE
sets how much it throws in: the overdrive up by half, the copies beating up to six times faster, the echo's feedback up
to 95 % (it saturates as it comes round, so it surges but stays bounded), and a kick to the energy. ENERGY > GRIT is
its sizzle: noise rung through narrow band passes an octave over the tones that sound, into the overdrive, so even the
grit is in tune (measured: 3 % of it falls between the harmonics; 0.2's white noise was gone with it).

## BOW: played by hand

BOW > HAND mixes from the drone as it is (0) to one bowed by your movement (100): at 100 a tone sounds only while it is
bowed. BOW > AXES: SPLIT (each way of moving bows its own voice: twisting the box flat the lead, swinging it forward and
back the chord, rocking it side to side the root, or the chord too with ROOT off) or ONE (any movement bows them all).
The bow's speed is the rotation's, from 15 degrees a second to 315.

- A bow change: turning back the other way gives the stroke an accent (BOW > BOW CHANGE); a circle never turns back,
  so it sustains, as the hurdy-gurdy's wheel does.
- BOW > PRESSURE: faster is brighter (measured: the overtones 10 dB up from 60 to 250 degrees a second), and past 85 %
  of full speed the string growls an octave down.
- The bow's place on the string: the roll held while bowing, averaged over half a second (so a swing does not move it):
  tipped one way darker, the other glassier.
- BOW > RING: how long a tone rings on once the bow leaves it (0.05 to 5 s).

## TAP: a knock

A knock is one jolt (over TAP > SENSE's threshold, 0.575 g at 50) on a still box, with nothing following it for 0.12 s;
jolts that keep coming are a shake. TAP > KNOCK says what it does:

- NEXT ROOT (the default): the whole drone fades over onto a new fundamental, over TAP > FADE seconds (8). There are two
  layers of voices: the one sounding fades out while the other, the same chord on the new root, fades in (equal power).
  TAP > NEXT ROOT is the step: LEAN (the cell you are leaning towards: tipped one step away, a fifth up; level, a fifth
  up anyway), or always 3/2, 4/3, 7/4, 8/7 or 9/8. The root stays within half an octave of the key's note (a fifth up
  is heard as a fourth down) and is shown under the ratio: TO 3/2 while it fades, ON 3/2 after. A knock during a fade
  waits for it. TAP > HOME fades back to the key.
- STRUM: every tone's level jumps and decays over TAP > STRUM seconds, the brightness opening with it; TANPURA plucks all
  its strings and starts its cycle of plucks again, GURDY buzzes.
- OFF: the box is deaf to knocks.

While both layers sound, the copies are shared out (40 at most, so 3 each with twelve tones).

## DIRT: the overdrive

The voices, after the energy, go through an overdrive: up to 32 dB of gain into a soft clipper whose negative side
clips later than its positive side (a fuzz's even harmonics), with first-order antiderivative anti-aliasing, a DC
blocker, then DIRT > TONE (a low pass from 800 Hz to 16 kHz). DIRT > DRIVE is its floor; the energy and a shake add to
it. DIRT > DIRT MIX is the wet and dry: at 0 the clean voices, at 100 only the driven ones. The clipper's ceiling holds
the level, so a crescendo into the dirt gains about 10 dB rather than 30. Ratios of small whole numbers saturate into
more of the same: a just fifth over its root, driven, puts its new overtones on the root's harmonics (measured: 3000
times the energy on them as between them). Measured with one pure voice at DRIVE 100: the 3rd harmonic 10 dB under the
fundamental, the 2nd 26, the 5th 15.

## ECHO

After the overdrive: an echo of ECHO > ECHO seconds (20 ms to 1.6 s; or ECHO SYNC: 1 beat, 3/4, 1/2 or 1/4 of the
flock's tempo), its time gliding like tape when it changes. What comes round is a little darker each time and
saturates, never above half scale: ECHO > FEEDBACK (0 to 95 %) can run long without blowing up. ECHO > ECHO MIX is the
wet and dry (equal power). Then the hall (SPACE, TAIL), with its own wet and dry.

## FEEDBACK: the room

HOWL's loop, tuned to the drone. The mic goes through narrow band passes on the tones that sound (each as strong as its
tone and its bow), and back out; only those notes can build up, so the room howls in just intonation. FEEDBACK >
FEEDBACK: AIR (through the speaker and the room: two DWELLs in one room feed each other), INNER (inside the box, one
sample later, with a little of the drone in it: it howls on headphones), BOTH, or OFF.

- LOOP: how close to howling (the inner loop howls from about 70); the energy pushes it further (TO LOOP), so a
  crescendo ends with the room taking over.
- HOWL LEVEL: the loudest a howl gets. A governor brings the loop's gain down fast whenever the howl passes it and up
  no faster than SWELL allows; the loop saturates at 1.3 times the level at every instant; what leaves the box rises
  at most 120 dB a second and never passes 1.25 times the level. Measured in the simulator: a room four times louder
  than one that just howls stays at HOWL LEVEL, its peaks under the ceiling.
- NARROW: the band passes' Q (10 to 200). SWELL: how slowly a howl may grow.
- A room pass turns the feedback off while it listens.

## TOGETHER: two DWELLs

Each DWELL sends twelve bytes of its own with the link's packets: its root and the root's number, its energy, its
FIFTH and its sounding cells. A board on an older core ignores them (they ride after the clock's data).

- SHARE ROOT: a knock (or HOME) gives the root a new number; a DWELL that hears a newer number fades to that root at
  its own FADE (the same number with a different root: the higher id wins). A board switched on late takes the room's.
- ENERGY: OWN, SHARED (this box's energy is at least 85 % of the other's: pump one, both swell) or TRADED (this box's
  falls by up to 70 % of the other's: they take turns).
- NEAR FAR: the other box's radio strength, smoothed over about half a second: -38 dBm (held together) is unison,
  -73 dBm (across a room) puts the two FAR BEATS apart (each half of it, opposite ways), so their tones beat. RSSI
  indoors jumps with how the boxes face; the smoothing keeps it slow, which suits a drone.
- FIFTH: UP or DOWN puts this box's 1/1 a just fifth (3/2 or 2/3) from the key; set it on one box.
- SPREAD: each DWELL sounds one tone of its own chord.

## RADIO: interference

RADIO > RADIO (OFF by default): WI-FI, BLUETOOTH or BOTH. Each Wi-Fi frame the antenna hears becomes a burst of crackle,
0.3 to 8 ms long by the frame's length, as loud as its signal is strong; each Bluetooth advert a 1 to 2 ms buzz, one
pitch for each of the three advertising channels. The bursts go into the overdrive and chop the drone under them (as a
phone's bursts do in a speaker). RADIO > INTERFERE sets how hard. A router's beacons give a tick ten times a second
(102.4 ms apart), a phone's traffic bursts. Nothing about who sent a frame is read or kept: only when, how strong, how
long.

With the link on, DWELL listens on the link's own channel (1), so the other boards' packets are in it too. With the link
off it listens on RADIO > CHANNEL. Bluetooth starts the first time it is wanted and then shares the antenna with Wi-Fi.
On the board the Bluetooth listener, Wi-Fi listening alongside the link, and the memory they need have not been tried.

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
| DREAM HOUSE | pure sines, 7-limit, four voices and the root; moving barely drives it |
| DRUMM | from near silence (REST -30 dB) into a bowed, driven wall: CLIMB 20 s, FALL 45 s, a long echo |
| MIASMA | a hurdy-gurdy, harsh from the start (DRIVE 70), shaking throws it to pieces, a short hard echo |
| IMPERIAL | a quiet reed (REST -24 dB), slow to build (30 s), barely driven, a 1.2 s echo, a long hall |
| BOWED BY HAND | it sounds only while you move it: twist, swing and rock bow the lead, the chord and the root |
| ONE BOW | any movement bows every voice, a long ring |
| GROWL | a hurdy-gurdy bowed hard: bow changes bite, flat out it growls an octave down |
| THE ROOM SINGS | FEEDBACK AIR: swing it and the room howls on the chord, held at -16 dB |
| HOWL HOME | FEEDBACK INNER: a reed whose own tones howl inside the box (headphones) |
| NIBLOCK | a bowed wall: DENSE 80, eight copies beating at 0.3 Hz, no root |
| ADNOS | a reed that slides: GLIDE, DRIFT 90 s |
| IN A ROOM | the ROOM source, 12 passes of 4 s set: run ROOM > ROOM PASS |
| TANPURA | four strings plucked every 0.8 s, the jawari at 55 |
| SHRUTI BOX | the reed with the bellows |
| HURDY GURDY | the wheel at 1.2 Hz, short strums (KNOCK STRUM) |
| STRUMMING | sines, six voices, strums ringing 5 s (Charlemagne Palestine; KNOCK STRUM) |
| CONRAD | a bow in 5-limit, four copies at 0.8 Hz |
| DAVACHI | six reed voices that slide over 45 s |
| DEEP LISTENING | a quiet reed in a long hall |
| INTERFERENCE | both radios cutting in, driven, with an echo |
| TEMPO BEATS | the clusters beat at the flock's tempo, the echo at 3/4 of a beat |
| YOUR TONE | the WAVE source: hold REC and sing |
| TARTINI | two sines and their difference tone |
| PYTHAGORAS | 3-limit: the fifths only; a knock moves the root a fifth |

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

### ENERGY

| Setting | CC | |
| - | - | - |
| CLIMB | 26 | Moving this long: full (1.0..60.0 s) |
| FALL | 27 | Still this long: back down (2.0..120.0 s) |
| REST LEVEL | 28 | How quiet it is unmoved (-40..0 dB) |
| TO DENSE | 29 | Moving thickens it (0..100 %) |
| TO DIRT | 30 | Moving drives it (0..100 %) |
| SHAKE | 31 | What a shake throws in (0..100 %) |
| GRIT | 32 | A shake's sizzle, in tune (0..100 %) |

### BOW

| Setting | CC | |
| - | - | - |
| HAND | 33 | 100: it sounds as you move (0..100 %) |
| AXES | 34 | Each motion its voice: ONE, SPLIT |
| RING | 35 | It rings on after the bow (0.05..5.0 s) |
| PRESSURE | 36 | Fast: bright, then a growl (0..100 %) |
| BOW CHANGE | 37 | A turn back: an accent (0..100 %) |

### TAP

| Setting | CC | |
| - | - | - |
| KNOCK | 38 | A strum, or a new root: OFF, STRUM, NEXT ROOT |
| NEXT ROOT | 39 | The step to the new root: LEAN, 3/2, 4/3, 7/4, 8/7, 9/8 |
| FADE | 40 | The new root fades in (0.5..60.0 s) |
| STRUM | 41 | How long a strum rings (0.2..8.0 s) |
| SENSE | 42 | How easily a knock counts (0..100 %) |
| HOME |  | Fade back to the key |

### LATTICE

| Setting | CC | |
| - | - | - |
| LIMIT | 44 | The primes the ratios use: 7 (YOUNG), 5, 3 |
| OCTAVE | 45 | Where 1/1 sits (1..4) |
| TILT STEP | 46 | Degrees per cell (6..20) |
| DWELL | 47 | Stay on a cell this long (0.1..2.0 s) |

### CLUSTER

| Setting | CC | |
| - | - | - |
| COPIES | 48 | Copies per tone at dense (1..8) |
| BEAT | 49 | How fast the copies beat (0.05..8.0 Hz) |
| BEAT SYNC | 50 | Beats at the flock's tempo: OFF, TEMPO |
| BRIGHT | 51 | Harmonics (0..100 %) |
| DIFF TONE | 52 | A sub at the difference (0..100 %) |

### SOURCE

| Setting | CC | |
| - | - | - |
| SOURCE | 53 | What each tone is: SINE, REED, BOW, TANPURA, SHRUTI, GURDY, WAVE, ROOM |
| PLUCK | 54 | Tanpura: plucks this often (0.2..4.0 s) |
| JAWARI | 55 | Tanpura: the bridge's buzz (0..100 %) |
| WHEEL | 56 | Gurdy: the wheel's turn (0.3..4.0 Hz) |
| BREATH | 57 | Shruti: the bellows (0..100 %) |
| BODY | 58 | Bow, reed: its wooden box (0..100 %) |

### DIRT

| Setting | CC | |
| - | - | - |
| DRIVE | 59 | Overdrive: more overtones (0..100 %) |
| TONE | 60 | Dark to bright after it (0..100 %) |
| DIRT MIX | 61 | Clean to driven (0..100 %) |

### ECHO

| Setting | CC | |
| - | - | - |
| ECHO | 62 | The echo's time (0.02..1.6 s) |
| ECHO SYNC | 63 | On the flock's beat: OFF, 1 BEAT, 3/4, 1/2, 1/4 |
| FEEDBACK | 70 | How often it comes back (0..95 %) |
| ECHO MIX | 71 | Dry to all echo (0..100 %) |

### SPACE

| Setting | CC | |
| - | - | - |
| SPACE | 72 | How much hall (0..100 %) |
| TAIL | 73 | A small room to endless (0..100 %) |

### FEEDBACK

| Setting | CC | |
| - | - | - |
| FEEDBACK | 74 | Air: speaker and room: OFF, AIR, INNER, BOTH |
| LOOP | 75 | How close to howling (0..100 %) |
| TO LOOP | 76 | Moving tips it into a howl (0..100 %) |
| HOWL LEVEL | 77 | The loudest a howl gets (-36..-6 dB) |
| NARROW | 78 | How narrow each note is (10.0..200.0) |
| SWELL | 79 | How slowly a howl grows (0.3..20.0 s) |

### TOGETHER

| Setting | CC | |
| - | - | - |
| SHARE ROOT | 80 | A knock moves both boxes: OFF, ON |
| ENERGY | 81 | Pump one: both, or turns: OWN, SHARED, TRADED |
| NEAR FAR | 82 | Apart, they beat: OFF, ON |
| FAR BEATS | 83 | The beating across a room (0.2..8.0 Hz) |
| FIFTH | 84 | This box a fifth away: OFF, UP, DOWN |
| SPREAD | 85 | Each board one tone: OFF, ON |

### RADIO

| Setting | CC | |
| - | - | - |
| RADIO | 86 | The air's traffic as noise: OFF, WI-FI, BLUETOOTH, BOTH |
| INTERFERE | 87 | How hard the air cuts in (0..100 %) |
| CHANNEL | 88 | Wi-fi channel, link off (1..13) |

### ROOM

| Setting | CC | |
| - | - | - |
| PASSES | 89 | Times through the room (1..30) |
| PASS LEN | 90 | Seconds per pass (2..ROOM_MAX_S s) |
| ROOM PASS |  | Play, record, again, again |

### SONG

| Setting | CC | |
| - | - | - |
| TEMPO | 92 | Shared with linked boards (50..200) |
| KEY | 93 | 1/1 is this note: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE | 94 | For chords from the link: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| FOLLOW | 95 | A linked chord moves 1/1: OFF, ON |
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

At full energy and clean, nominal peaks around -12 dBFS with four voices; at rest, REST LEVEL under that (-14 dB by
default). A cluster keeps a pure tone's average level and peaks up to twice as high. Fully driven, the overdrive's
ceiling puts peaks near -6 dBFS whatever goes in; the echo and the hall add to it, and the core's limiter at -1 dBFS
catches the rest. The root at OCTAVE 3 is 65 Hz: the small speaker loses it, the line out has it.
