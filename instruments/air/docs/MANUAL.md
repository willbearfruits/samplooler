# AIR manual

## What happens when you swing

1. The motion sensor's gyro reads how fast the box turns, a hundred times a second.
2. A stroke is a turn that climbs over the onset threshold, peaks and falls. As soon as the rate has dropped
   15 % from its peak, the hit fires: that is a few hundredths of a second after the fastest point of the
   swing, before your hand has stopped, which is where a stick would land.
3. Which way the box turned picks the pad. Turning about the sideways axis (the top edge dipping or rising) is
   FRONT or BACK; turning about the other two (the box swung to a side, flat or on edge) is LEFT or RIGHT.
   The peak rate is the velocity.
4. The hand coming back from a stroke turns the other way, slower. A stroke opposite to the last one, within
   0.3 s of it and under 80 % of its speed, is taken as that return and does not sound. A deliberate flick back
   as hard as the stroke does.
5. The reset: after a hit, the same pad does not sound again until the box has turned back the other way by 10
   degrees or has rested for 0.3 s. So one long turn is one hit, not one every 15 degrees; a roll on one pad is
   a swing back and forth; a roll between two pads needs no reset at all.
6. QUANTIZE: the hit waits for the next eighth or sixteenth of the beat. The beat is the loop's, else the flock's
   (a board on the link), else FEEL > TEMPO. Off, hits sound as they come.

The screen shows a drummer at a kit. A hit swings the arm to the piece (the foot to the kick) and the piece
answers: the hat claps shut or flies open, the snare drops, the kick bulges, and the pad's name floats up. The
compass on the right shows the stroke the sensor sees as it happens (the line from the middle) and the pad it
picked; the letters around it are the four pads' drums. With a loop, the loop runs around the compass as an orbit
with a mark per hit and a hand for where it is. A small antenna in the top right corner means another board is
heard on the link; it blinks when your loop and the flock disagree on the tempo.

## The breaks and the printed kit

The pads can hold PART 1 to PART 4: the four equal quarters of BREAK (AMEN, THINK, APACHE, FUNKY, IMPEACH, BOOM BAP,
BLAST, DOOM, or DRUMS). The breaks are WRECK's: a synthesized kit playing the classic patterns, through the room it
was played in (PLAYED IN) and a PRINT (clean, tape, vinyl, a 12-bit sampler, dusty), rendered in the background
when BREAK or any setting on that page changes (the PRINT pill shows how far; the old break plays until the new
one is whole). DRUMS is one drum per quarter, a kick, a snare, a hat and an open hat, so AIR KIT is that kit
printed the same way. STRETCH plays a break's parts at the grid's tempo (TEMPO, or the loop's or the flock's), so
the Amen at 136 fits a flock at 120; DRUMS is never stretched. DRUM TUNE, TONE, RING, SHUFFLE, GHOSTS, HUMAN and
TAKE shape the drummer; the tilt and TUNE still speed the parts up and down after that.

With two boards, LINK > HANDS LEFT on one and RIGHT on the other: a break of two bars gives the left hand the first
bar's four beats and the right hand the second's. Both boards keep the same beat over the link, so quantized hits
from both hands land together.

## The loop

- **Hold REC** and play. Let go: what you played loops, as long as you held REC. Alone, your first hit within a
  moment of pressing REC becomes the downbeat, and the loop's length and beats set the flock's tempo (BEATS: AUTO
  guesses between 75 and 165 BPM).
- With a board on the link, the loop is cut to whole beats of the flock's tempo and starts on the beat nearest
  to where you pressed REC, so it sits on the shared grid. A tap that starts a stopped loop waits for the next beat.
- **Hold REC again** while it plays to add a layer.
- **Tap REC** (shorter than 0.3 s) to stop it; tap again to start it from the top. A tap never records.
- To start again from nothing, stop it and hold REC; or LOOP > CLEAR LOOP. LOOP > UNDO LAST takes the last layer away.
- GRID and STRENGTH pull the hits towards a grid as they play, without changing what was recorded. SWING makes
  the offbeats late. MUTATE drops hits and swaps pads at random, more the higher it is.
- When another board changes the tempo while your loop plays, your loop keeps its own tempo (the link flags the
  clash: the antenna blinks). Clear the loop and record it again on the new beat.

## Samples

Hold the knob (or SAMPLE > SAMPLE in the menu). The screen says HIT THE AIR: swing for the pad you want. Then it
listens: make a sound at the mic. The take starts at the first sound, keeps 3 ms from before it so the attack is
whole, and stops when the sound has died away for 0.15 s (SAMPLE > ENDS: SOUND DIES) or at SAMPLE > LONGEST (ENDS:
AT LONGEST, or tap the knob to stop it early). The drums are quiet while it listens so the mic hears only you.
The pad's setting becomes SAMPLE and the sample is written to flash (the output is muted for a moment while it is
written). A sample plays at the drums' level whatever its own; TUNE and the tilt play it faster or slower.
SAMPLE > FORGET ALL deletes the four files and sets any SAMPLE pad back to its drum.

A pad set to SAMPLE with no sample in it plays nothing and says so.

## The stick

| | |
| - | - |
| up | a roll of the last drum: further up, faster (1/8, 1/16, 1/32 of a beat with a loop or a linked tempo; slow, medium, fast without) |
| down | repeats the last 1/8, 1/16 or 1/32 of a beat of everything, while held |
| left / right | a filter on everything: left darker, right thinner (adds to BUS > FILTER) |

## The bus

FILTER, DRIVE (dirt), CRUSH (a sample held at a lower rate with fewer bits: full is 2 kHz and 4 bits), PUNCH (a
compressor), ECHO with ECHO TIME and REPEATS (a tape echo), ROOM and ROOM SIZE, and GRAIN: pieces of the last
half second of everything, GRAIN SIZE long, four at a time, some at half or double speed, so a hit keeps coming
back in bits. The stick's repeat and the grain share one buffer.

## The tilt

With FEEL > TILT: TUNE, the bottom edge down tunes the drums up, up tunes them down (5 semitones at 30 degrees).
The tilt is read while the box is still, before a stroke, so the swing itself does not bend the note.

## The menu

The menu is a strip along the bottom of the screen; the instrument keeps playing while you are in it. Press the knob for the menu. Turn to choose, press to open or edit, press again to go back up; hold the
knob to go back to playing. After you edit a setting, turning the knob outside the menu changes that setting
(choose SOUND in the menu to make the knob change kits again).

Every setting with a CC below is on MIDI, both ways.

### SOUND

| | |
| - | - |
| SOUND | the factory kits, then MY EDIT and your user kits |
| SAVE AS | store the current settings as USER 1..8 |
| REVERT | back to the kit as it was saved |

<!-- tables -->
### PADS

| Setting | CC | |
| - | - | - |
| FRONT | 20 | Hit forward: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| LEFT | 21 | Hit left: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| RIGHT | 22 | Hit right: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| BACK | 23 | Hit back: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| LEVEL | 24 | How loud the drums are (0..100 %) |
| TUNE | 25 | Everything up or down (-12..12 st) |
| DECAY | 26 | Shorter or longer drums (0.25..4.0 x) |
| CLICK | 27 | How hard the kick starts (0..100 %) |

### BREAK

| Setting | CC | |
| - | - | - |
| BREAK | 28 | Four parts, four pads |
| PRINT | 29 | Tape, a record, a sampler: CLEAN, TAPE, VINYL, 12-BIT, DUSTY |
| PLAYED IN | 30 | The room it was played in (0..100 %) |
| DRUM TUNE | 31 | The drums up or down (-12..12 st) |
| TONE | 32 | Dark or bright (0..100 %) |
| RING | 33 | Muffled or ringing drums (0..100 %) |
| SHUFFLE | 34 | The break swung (0..100 %) |
| GHOSTS | 35 | Soft snare notes (0..100 %) |
| HUMAN | 36 | How loose the drummer is (0..100 %) |
| TAKE | 37 | Another take of it (1..9) |
| STRETCH | 41 | A break at the beat's speed: OFF, ON |

### FEEL

| Setting | CC | |
| - | - | - |
| SENSE | 42 | How small a swing counts (0..100 %) |
| VELOCITY | 43 | How hard to swing for loud: SOFT, NORMAL, HARD, FIXED |
| SIDES | 44 | Which side is the kick: KICK LEFT, KICK RIGHT |
| FORWARD | 45 | Which way is in front: TIP AWAY, TIP TO ME |
| TILT | 46 | Bottom edge down: tuned up: OFF, TUNE |
| STICK Y | 47 | Up rolls, down repeats: OFF, ROLL, ROLL+REPEAT |
| QUANTIZE | 48 | Hits wait for the grid: OFF, 1/8, 1/16 |
| TEMPO | 49 | The grid alone, no loop (60..200) |

### SAMPLE

| Setting | CC | |
| - | - | - |
| SAMPLE |  | Hit the air, make a sound |
| LONGEST | 51 | A sample is cut here (0.25..1.0 s) |
| ENDS | 52 | When a sample stops: SOUND DIES, AT LONGEST |
| FORGET ALL |  | Samples gone, drums back |

### LOOP

| Setting | CC | |
| - | - | - |
| BEATS | 54 | Auto: it guesses: AUTO, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 |
| GRID | 55 | What hits are pulled to: OFF, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| STRENGTH | 56 | How hard they are pulled (0..100 %) |
| SWING | 57 | Late offbeats (0..75 %) |
| MUTATE | 58 | Hits drop out or change (0..100 %) |
| LOOP LEVEL | 59 | How loud the loop plays (0..100 %) |
| UNDO LAST |  | Take away the last layer |
| CLEAR LOOP |  | Erase the loop |

### BUS

| Setting | CC | |
| - | - | - |
| FILTER | 62 | Down: dark  up: thin (-100..100 %) |
| DRIVE | 63 | Dirt (0..100 %) |
| CRUSH | 70 | Fewer bits, a lower rate (0..100 %) |
| PUNCH | 71 | Squeeze: fatter, louder (0..100 %) |
| ECHO | 72 | How much goes to the echo (0..100 %) |
| ECHO TIME | 73 | Time between repeats (0.04..0.7 s) |
| REPEATS | 74 | How long the echo goes on (0..90 %) |
| ROOM | 75 | How much room (0..100 %) |
| ROOM SIZE | 76 | Small box to big hall (0..100 %) |
| GRAIN | 77 | The last moment, in pieces (0..100 %) |
| GRAIN SIZE | 78 | How long each piece is (10..250 ms) |

### LINK

| Setting | CC | |
| - | - | - |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| HANDS |  | Two boards: a bar each: ONE, LEFT, RIGHT |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

### SYSTEM

Volume, speaker, mic gain, tilt setup, motion, the joystick, the REC button, MIDI channel, MIDI CC out, LED,
hints, screen saver, ABOUT, RESET ALL: the same on every instrument of this family (see the core's notes).

## MIDI

On channel 10, out: a note for every hit (the pad's drum's General MIDI note: kick 36, snare 38, hat 42, open hat
46, tom 45, clap 39, rim 37, cowbell 56, shaker 70, conga 63, zap 81; a SAMPLE or OFF pad sends its pad's default)
with the stroke's velocity. In, on the channel set in SYSTEM: notes play the pads by the same numbers (a note no
pad has goes to pad note mod 4); the settings on their CCs; program change picks a kit; CC 64 is REC.

## Tips

- Hold the box so that the screen faces up and the bottom edge is towards you, then swing from the wrist. Short,
  sharp strokes read best. Let the hand come back gently.
- If the pads come out swapped, FEEL > SIDES and FEEL > FORWARD turn them round. The sensor's axes depend on
  how the box sits in your hand and on the tilt setup.
- FEEL > SENSE: lower if small movements count, higher if soft strokes are missed. VELOCITY: SOFT if everything
  comes out quiet, FIXED for a drum machine feel. On the first board (2026-10-22) SENSE 60 with no reset rule
  fired a hit every 15 degrees of a slow turn; the reset rule and SENSE 50 came from that.
- Swing a break's parts in order for the break; swing them out of order, or two of them, for your own.
- A hat stroke chokes an open hat, as on a kit.
- The tuned drums (TOM, CONGA, COWBELL, ZAP) sit in the flock's key, a different degree per pad, so a kit of
  toms plays a phrase.

## When something is wrong

| | |
| - | - |
| nothing sounds when I swing | SYSTEM > MOTION must be on and the tilt set up (SYSTEM > TILT SETUP). The compass's line should move with the box. Raise FEEL > SENSE |
| hits come too easily | lower FEEL > SENSE; swing from the wrist, not the arm |
| the pad is wrong | FEEL > SIDES, FEEL > FORWARD |
| a sample never starts | the mic must hear a sound well over the room: SYSTEM > MIC GAIN up, or get closer |
| the sample was cut short | SAMPLE > ENDS: AT LONGEST, and LONGEST up |
| the loop drifts against the others | it was recorded alone or at another tempo; CLEAR LOOP and record it again while linked |
