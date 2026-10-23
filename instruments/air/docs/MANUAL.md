# AIR manual

## The beat

AIR keeps a beat from the moment it is on: LOOPS > TEMPO (100 to start), four beats to a bar. The drummer on the
screen nods on every beat and the LED blinks white, brighter on the first beat of a bar. With a board on the link
the beat is the flock's. Every hit you play lands on the nearest sixteenth of that beat (QUANTIZE), and a loop is
always whole bars of it, so what you play is in time without you thinking about it.

## What happens when you swing

1. The motion sensor's gyro reads how fast the box turns, a hundred times a second; its accelerometer, a thousand.
2. A stroke is a turn that climbs over the onset threshold, peaks and falls. As soon as the rate has dropped
   15 % from its peak, the hit fires: that is a few hundredths of a second after the fastest point of the
   swing, before your hand has stopped, which is where a stick would land.
3. Which way the box turned picks the pad. Turning about the sideways axis (the top edge dipping or rising) is
   FRONT or BACK; turning about the other two (the box swung to a side, flat or on edge) is LEFT or RIGHT.
   The peak rate is the velocity.
4. CONFIRM: a hit also needs a jolt. A swing throws the accelerometer well off 1 g; a slow turn, a walk or a
   wobble does not. So the box can be carried and tilted without a drum sounding.
5. The hand coming back from a stroke turns the other way, slower. A stroke opposite to the last one, within
   0.3 s of it and under 80 % of its speed, is taken as that return and does not sound. A deliberate flick back
   as hard as the stroke does.
6. The reset: after a hit, the same pad does not sound again until the box has turned back the other way by 10
   degrees or has rested for 0.3 s. So one long turn is one hit, not one every 15 degrees; a roll on one pad is
   a swing back and forth; a roll between two pads needs no reset at all.
7. QUANTIZE: the hit goes to the nearest sixteenth (or eighth) of the beat. A hit that comes a little late sounds
   at once and is written on the line it was meant for; one that comes a little early waits for its line. So
   nothing is ever made later than you played it, and the loop holds what you meant.

FEEL > SWING SETUP learns how you swing: swing forward four times as you would play, and the hit threshold and the
full-velocity swing are set from those (a hit from 40 % of your usual swing, full at 130 %). SENSE scales either
way of setting them: higher, smaller swings count.

The screen shows a drummer at a kit. A hit swings the arm to the piece (the foot to the kick) and the piece
answers: the hat claps shut or flies open, the snare drops, the kick bulges, and the pad's name floats up. The
compass on the right shows the stroke the sensor sees as it happens (the line from the middle) and the pad it
picked; the letters around it are the four pads' drums. Around the compass the beat runs as a clock: one hand,
one turn per bar (or per longest loop), beat marks outside, and a ring per loop with a mark per hit. A small
antenna at the top means another board is heard on the link (a number: how many); it blinks when your loops and
the flock disagree on the tempo.

## Samples

Tap REC and make a sound at the mic: say it, clap it, hum it, bang something. The take starts at the first sound,
keeps 3 ms from before it so the attack is whole, and stops when the sound has died away for 0.15 s (PADS > ENDS:
SOUND DIES) or at PADS > LONGEST (ENDS: AT LONGEST, or tap REC or the knob to stop it early). The drums are quiet
while it listens so the mic hears only you.

The sound lands on the next pad clockwise: FRONT first, then RIGHT, BACK, LEFT, and round again (the screen names
the pad while it listens and the next one when it is kept). Four taps, four sounds, a kit of your own. The pad's
setting becomes SAMPLE and the sample is written to flash (the output is muted for a moment while it is written),
so it is there after a power cycle. A sample plays at the drums' level whatever its own; TUNE and the tilt play
it faster or slower. PADS > SAMPLE in the menu does the same as the tap. PADS > FORGET ALL deletes the four files,
sets any SAMPLE pad back to its drum and starts again from FRONT.

A pad set to SAMPLE with no sample in it plays nothing and says so.

## The loops

- **Hold REC and play.** The loop starts on the next bar (the screen counts in: REC IN 3, 2, 1; pressed within a
  beat after a bar line it starts on that line) and runs while you hold. Let go: it ends on the nearest bar line,
  one bar at least, eight at most. Let go a little early and it records on to the line; a little late and what
  you played after the line goes to the start of the next pass. It plays from there.
- Each hold is a **new loop**, on top of the ones playing: up to four, stacked. That is how you layer: the kick
  first, the hats on top, a snare over that. The fifth hold records over the newest.
- A loop is in beats, not seconds: change TEMPO and every loop follows.
- **Hold the knob**: every loop stops. Hold it again: they go, on the beat they would have reached.
- **Each loop on or off**: push the stick, up for loop 1, right for 2, down for 3, left for 4 (FEEL > STICK:
  LOOPS); or LOOPS > LOOP 1..4 in the menu. The ring on the screen is dotted while a loop plays.
- **Erase**: LOOPS > ERASE LAST takes the newest away, ERASE ALL every one. A hold with no hits in it leaves
  nothing.
- LOOPS > SWING makes the offbeat sixteenths late. MUTATE drops hits and swaps pads at random, more the higher it
  is. LOOP LEVEL is how loud the loops play under what you play live.

## The flock

LINK is on from the start, GROUP 1: any board of the family nearby is heard, nothing to pair. The beat is shared:
a BAND, a HUFF, a WRECK, a MANTRA, the Samplooler, another AIR, all on one grid.

- **Who sets the tempo.** The first loop anywhere locks the flock's tempo. Alone, or before any loop exists
  anywhere, your TEMPO is the flock's: turn it and a BAND follows. Once a board on the link has a loop, TEMPO here
  shows that tempo and cannot be turned (a message says whose beat it is) until the loop is cleared there. Your
  own loops do not lock you: they are in beats and follow your TEMPO. A board that joins late never moves a
  loop that is playing.
- With AIR's loops playing, the others' takes snap to AIR's beat: a BAND re-bases its loop to a new tempo; a HUFF
  or a Samplooler starts its loop on the beat and stays on it while the tempo holds (theirs are audio loops: they
  cannot re-speed without re-pitching).
- **Two AIRs**, one in each hand: LINK > HANDS AUTO (the default) gives the lower-numbered board the left hand
  and the other the right, and says so on the screen; with a break on the pads the left hand holds the first bar
  and the right the second. ONE, LEFT and RIGHT set it by hand.
- When another board changes the tempo while your loops play, your loops keep theirs (the link flags the clash:
  the antenna blinks). ERASE ALL and record again on the new beat.

## The breaks and the printed kit

The pads can hold PART 1 to PART 4: the four equal quarters of BREAK (AMEN, THINK, APACHE, FUNKY, IMPEACH, BOOM BAP,
BLAST, DOOM, or DRUMS). The breaks are WRECK's: a synthesized kit playing the classic patterns, through the room it
was played in (PLAYED IN) and a PRINT (clean, tape, vinyl, a 12-bit sampler, dusty), rendered in the background
when BREAK or any setting on that page changes (the PRINT pill shows how far; the old break plays until the new
one is whole). DRUMS is one drum per quarter, a kick, a snare, a hat and an open hat, so AIR KIT is that kit
printed the same way. STRETCH plays a break's parts at the beat's speed, so the Amen at 136 fits a beat of 120;
DRUMS is never stretched. DRUM TUNE, TONE, RING, SHUFFLE, GHOSTS, HUMAN and TAKE shape the drummer; the tilt and
TUNE still speed the parts up and down after that.

## The stick

FEEL > STICK: LOOPS (the four loops on and off, above) or ROLL+REPEAT:

| | |
| - | - |
| up | a roll of the last drum: further up, faster (1/8, 1/16, 1/32 of a beat) |
| down | repeats the last 1/8, 1/16 or 1/32 of a beat of everything, while held |
| left / right | a filter on everything: left darker, right thinner (adds to FX > FILTER) |

## FX

FILTER, DRIVE (dirt), CRUSH (a sample held at a lower rate with fewer bits: full is 2 kHz and 4 bits), PUNCH (a
compressor), ECHO with ECHO TIME and REPEATS (a tape echo), ROOM and ROOM SIZE, and GRAIN: pieces of the last
half second of everything, GRAIN SIZE long, four at a time, some at half or double speed, so a hit keeps coming
back in bits. The stick's repeat and the grain share one buffer.

## The tilt

With FEEL > TILT: TUNE, the bottom edge down tunes the drums up, up tunes them down (5 semitones at 30 degrees).
The tilt is read while the box is still, before a stroke, so the swing itself does not bend the note.

## The menu

The menu is a strip along the bottom of the screen; the instrument keeps playing while you are in it. Press the
knob for the menu. Turn to choose, press to open or edit, press again to go back up; hold the knob to go back to
playing. After you edit a setting, turning the knob outside the menu changes that setting (choose SOUND in the menu
to make the knob change kits again).

Every setting with a CC below is on MIDI, both ways.

### SOUND

| | |
| - | - |
| SOUND | the factory kits, then MY EDIT and your user kits |
| SAVE AS | store the current settings as USER 1..8 |
| REVERT | back to the kit as it was saved |

<!-- tables -->
### LOOPS

| Setting | CC | |
| - | - | - |
| TEMPO | 20 | The beat, alone (60..200) |
| LOOP 1 |  | Up on the stick: EMPTY, OFF, ON |
| LOOP 2 |  | Right on the stick: EMPTY, OFF, ON |
| LOOP 3 |  | Down on the stick: EMPTY, OFF, ON |
| LOOP 4 |  | Left on the stick: EMPTY, OFF, ON |
| ERASE LAST |  | The newest loop goes |
| ERASE ALL |  | Every loop goes |
| QUANTIZE | 27 | Hits land on the grid: OFF, 1/8, 1/16 |
| SWING | 28 | Late offbeats (0..75 %) |
| LOOP LEVEL | 29 | How loud the loops play (0..100 %) |
| MUTATE | 30 | Hits drop out or change (0..100 %) |

### PADS

| Setting | CC | |
| - | - | - |
| FRONT | 31 | Hit forward: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| LEFT | 32 | Hit left: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| RIGHT | 33 | Hit right: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| BACK | 34 | Hit back: this drum: SAMPLE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP, PART 1, PART 2, PART 3, PART 4, OFF |
| SAMPLE |  | Make a sound: the next pad |
| FORGET ALL |  | Samples gone, drums back |
| LONGEST | 37 | A sample is cut here (0.25..1.0 s) |
| ENDS | 38 | When a sample stops: SOUND DIES, AT LONGEST |
| LEVEL | 39 | How loud the drums are (0..100 %) |
| TUNE | 40 | Everything up or down (-12..12 st) |
| DECAY | 41 | Shorter or longer drums (0.25..4.0 x) |
| CLICK | 42 | How hard the kick starts (0..100 %) |

### FEEL

| Setting | CC | |
| - | - | - |
| SWING SETUP |  | Four swings: it learns you |
| SENSE | 44 | How small a swing counts (0..100 %) |
| VELOCITY | 45 | How hard to swing for loud: SOFT, NORMAL, HARD, FIXED |
| SIDES | 46 | Which side is the kick: KICK LEFT, KICK RIGHT |
| FORWARD | 47 | Which way is in front: TIP AWAY, TIP TO ME |
| CONFIRM | 48 | A hit needs a jolt: OFF, ON |
| TILT | 49 | Bottom edge down: tuned up: OFF, TUNE |
| STICK | 50 | What the stick does: LOOPS, ROLL+REPEAT, OFF |

### FX

| Setting | CC | |
| - | - | - |
| FILTER | 51 | Down: dark  up: thin (-100..100 %) |
| DRIVE | 52 | Dirt (0..100 %) |
| CRUSH | 53 | Fewer bits, a lower rate (0..100 %) |
| PUNCH | 54 | Squeeze: fatter, louder (0..100 %) |
| ECHO | 55 | How much goes to the echo (0..100 %) |
| ECHO TIME | 56 | Time between repeats (0.04..0.7 s) |
| REPEATS | 57 | How long the echo goes on (0..90 %) |
| ROOM | 58 | How much room (0..100 %) |
| ROOM SIZE | 59 | Small box to big hall (0..100 %) |
| GRAIN | 60 | The last moment, in pieces (0..100 %) |
| GRAIN SIZE | 61 | How long each piece is (10..250 ms) |

### BREAK

| Setting | CC | |
| - | - | - |
| STRETCH | 62 | A break at the beat's speed: OFF, ON |
| BREAK | 63 | Four parts, four pads |
| PRINT | 70 | Tape, a record, a sampler: CLEAN, TAPE, VINYL, 12-BIT, DUSTY |
| PLAYED IN | 71 | The room it was played in (0..100 %) |
| DRUM TUNE | 72 | The drums up or down (-12..12 st) |
| TONE | 73 | Dark or bright (0..100 %) |
| RING | 74 | Muffled or ringing drums (0..100 %) |
| SHUFFLE | 75 | The break swung (0..100 %) |
| GHOSTS | 76 | Soft snare notes (0..100 %) |
| HUMAN | 77 | How loose the drummer is (0..100 %) |
| TAKE | 78 | Another take of it (1..9) |

### LINK

| Setting | CC | |
| - | - | - |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| HANDS |  | Two airs: a bar each: AUTO, ONE, LEFT, RIGHT |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

### SYSTEM

Volume, speaker, mic gain, tilt setup, motion, the joystick, the REC button, MIDI channel, MIDI CC out, LED,
hints, screen saver, ABOUT, RESET ALL: the same on every instrument of this family (see the core's notes).

## MIDI

On channel 10, out: a note for every hit (the pad's drum's General MIDI note: kick 36, snare 38, hat 42, open hat
46, tom 45, clap 39, rim 37, cowbell 56, shaker 70, conga 63, zap 81; a SAMPLE or OFF pad sends its pad's default)
with the stroke's velocity. In, on the channel set in SYSTEM: notes play the pads by the same numbers (a note no
pad has goes to pad note mod 4), on the grid like a swing; the settings on their CCs; program change picks a kit;
CC 64 is REC (a short one samples, a long one loops).

## Tips

- Hold the box so that the screen faces up and the bottom edge is towards you, then swing from the wrist. Short,
  sharp strokes read best. Let the hand come back gently.
- Run FEEL > SWING SETUP once: four swings, and the thresholds are yours.
- If the pads come out swapped, FEEL > SIDES and FEEL > FORWARD turn them round. The sensor's axes depend on
  how the box sits in your hand and on the tilt setup.
- Record the loops one layer at a time: a bar of kick, then a bar of hats. A 1-bar loop and a 2-bar loop sit
  together; the clock on the screen turns once per the longest.
- Swing a break's parts in order for the break; swing them out of order, or two of them, for your own.
- A hat stroke chokes an open hat, as on a kit.
- The tuned drums (TOM, CONGA, COWBELL, ZAP) sit in the flock's key, a different degree per pad, so a kit of
  toms plays a phrase.

## When something is wrong

| | |
| - | - |
| nothing sounds when I swing | SYSTEM > MOTION must be on and the tilt set up (SYSTEM > TILT SETUP). The compass's line should move with the box. Raise FEEL > SENSE, or run SWING SETUP; FEEL > CONFIRM off if soft swings are lost |
| hits come too easily | lower FEEL > SENSE; swing from the wrist, not the arm; CONFIRM on |
| hits feel late | they wait for the nearest sixteenth: QUANTIZE 1/8 is looser, OFF plays as you swing |
| the pad is wrong | FEEL > SIDES, FEEL > FORWARD |
| a sample never starts | the mic must hear a sound well over the room: SYSTEM > MIC GAIN up, or get closer |
| the sample was cut short | PADS > ENDS: AT LONGEST, and LONGEST up |
| TEMPO will not turn | a board on the link has a loop: the beat is its until that loop is cleared |
| the loops drift against the others | they were recorded at another tempo; ERASE ALL and record again while linked |
