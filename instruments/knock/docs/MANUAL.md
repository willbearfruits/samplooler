# KNOCK manual

## What happens when you knock

1. The mic hears the knock: when it happened and how hard.
2. The motion sensor felt which way the box was pushed, so KNOCK knows the side you hit. That takes about
   10 ms, so the drum itself starts 10 to 20 ms after the knock.
3. The side's drum plays (its VOICE), and the knock's own sound rings the kit's BODY: tuned resonators
   (wood, marimba, bell, glass, tabla, steel pan, drum skin, metal). With PITCH: ONE the body rings at
   the instant of the knock, and rubs and scrapes ring it too. With PITCH: BY SIDE each side is its own
   note, and the body waits for the side like the drum does.

The screen shows the box. The side you hit flashes and the box jumps away from your knuckles. On the
right is the mic's level against the knock threshold (the line), or, once there is a loop, the loop as an
orbit with the tempo in the middle. At the bottom the mic's level scrolls past with a mark for every
hit, or, with a loop, the loop's five lanes (one per side) and its playhead.

## The loop

- **Hold REC** and knock. Let go: what you played loops, as long as you held REC. If your first knock
  came within a moment of pressing REC, it becomes the downbeat.
- **Hold REC again** while it plays to add a layer.
- **Tap REC** (shorter than 0.3 s) to stop it; tap again to start it from the top. A tap never records.
- To start again from nothing, stop it and hold REC; or LOOP > CLEAR LOOP.
- LOOP > UNDO LAST takes the last layer away.
- The loop keeps each knock's own sound, so its BODY rings the way you knocked it.
- GRID and STRENGTH pull the hits towards a grid as they play, without changing what was recorded: turn
  the grid off and they are back where you knocked them.

## The stick

| | |
| - | - |
| up | a roll of the last drum: further up, faster (1/8, 1/16, 1/32 of a beat with a loop; slow, medium, fast without) |
| down | repeats the last 1/8, 1/16 or 1/32 of a beat of everything, while held |
| left / right | a filter on everything: left darker, right thinner (adds to BUS > FILTER) |

## The menu

Press the knob for the menu. Turn to choose, press to open or edit, press again to go back up; hold the
knob to go back to playing. While you edit, the box keeps playing so you hear the change. After you edit
a setting, turning the knob outside the menu changes that setting (choose SOUND in the menu to make the
knob change sounds again).

Every setting is on a MIDI CC (listed), both ways.

### SOUND

| | |
| - | - |
| SOUND | the factory sounds, then MY EDIT and your user sounds |
| SAVE AS | store the current settings as USER 1..8 |
| REVERT | back to the sound as it was saved |

### KIT: the drums (VOICE)

| Setting | CC | |
| - | - | - |
| FACE, LEFT, RIGHT, TOP, BOTTOM | 20-24 | the drum on each side: NONE, KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, ZAP. NONE leaves only the body. TOM, CONGA, COWBELL and ZAP are tuned to the side's note in the SCALE |
| DRUM LEVEL | 25 | the drums' level against the body |
| TUNE | 26 | everything up or down, in semitones (tilt adds to it) |
| DECAY | 27 | shorter or longer drums |
| CLICK | 28 | how hard the kick starts |

### BODY: what your knock rings

| Setting | CC | |
| - | - | - |
| BODY | 29 | OFF, WOOD, MARIMBA, BELL, GLASS, TABLA, STEEL PAN, MEMBRANE, METAL |
| BODY LEVEL | 30 | |
| PITCH | 31 | ONE: one note, rings at once, rubs and scrapes ring too. BY SIDE: each side its note of the SCALE |
| ROOT | 32 | the body's note (C2..C6) |
| SCALE | 33 | the five sides' notes: PENTA, MINOR, MAJ 7, MIN 7, FIFTHS, HIJAZ, WHOLE, UNISON |
| RING | 34 | how long it rings |
| BRIGHT | 35 | how much of the knock's top end reaches the body |
| TOUCH | 36 | how much of each knock goes in (3..32 ms); longer lets scrapes and rubs through |

### FEEL: how knocks are read

| Setting | CC | |
| - | - | - |
| SENSE | 37 | how soft a knock still counts. The mic meter's line is the threshold |
| SIDES | 38 | SIDE: by the side you hit. TONE: by the sound of the knock (dull, middle, sharp: face, left, right), for when the box lies on something soft or has no motion sensor. ONE DRUM: every knock is the face |
| VELOCITY | 39 | SOFT, NORMAL, HARD (how hard you must knock for loud), FIXED |
| TILT | 40 | OFF, TUNE, TUNE+MUTATE |
| STICK Y | 41 | OFF, ROLL, ROLL+REPEAT |

### LOOP

| Setting | CC | |
| - | - | - |
| BEATS | 42 | beats in the loop; AUTO picks 4, 2, 8 (and so on) so the tempo lands between 75 and 165 |
| GRID | 43 | OFF, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| STRENGTH | 44 | how far hits are pulled to the grid |
| SWING | 45 | how late the offbeats come |
| MUTATE | 46 | hits drop out, change drum or change velocity as the loop plays (leaning adds to it) |
| LOOP LEVEL | 47 | |
| UNDO LAST, CLEAR LOOP | | |

### BUS: everything together

| Setting | CC | |
| - | - | - |
| FILTER | 50 | down: low pass, up: high pass (the stick adds to it) |
| DRIVE | 51 | saturation |
| PUNCH | 52 | a compressor: fatter and louder |
| ECHO | 53 | how much goes into a tape-like echo |
| ECHO TIME | 54 | 40..700 ms |
| REPEATS | 55 | the echo's feedback |
| ROOM | 56 | a small room |
| ROOM SIZE | 57 | |

### SYSTEM

Volume, mic gain, tilt setup, motion on/off, joystick (AUTO uses one when it is fitted) and its
orientation, MIDI channel, MIDI CC out (the lean on CC 12/13, the stick on CC 14/15), the LED (it
flashes each side's colour), hints, the screen saver, ABOUT (version, CPU, memory), RESET ALL.

## MIDI

| | |
| - | - |
| out, channel 10 | each hit: kick 36, rim 37, snare 38, clap 39, closed hat 42, low tom 45, open hat 46, cowbell 56, high conga 63, maracas 70 (shaker), open triangle 81 (zap); a side set to NONE sends 36 / 38 / 42 / 45 / 39 |
| out, MIDI CH | settings as CC 20+ when changed on the box; program change when the sound changes; CC 12-15 lean and stick |
| in, MIDI CH | notes play the side with that drum; CC 20+ set settings; program change picks a sound; note 60 = REC; note 64 = freeze |

## Tips

- Knock with a knuckle near the middle of a side. On a hard table the face is easiest; lift the box to
  reach the edges.
- If knocks are missed, raise SENSE or MIC GAIN; if it plays by itself, lower them. The line on the mic
  meter is where a knock starts to count.
- If the sides come out wrong, run SYSTEM > TILT SETUP again, or use SIDES: TONE.
- The box listens to itself: with the speaker on, it learns how loud its own drums are in the mic and
  ignores them; the first loud seconds after you turn the volume up can still trigger it.
- With PITCH: ONE, the body rings at the instant of your knock: that is the one to play fast or to rub.

## When something is wrong

- The screen shows CRASH, BROWNOUT or WATCHDOG for a moment after a restart that was not clean; after
  three in a row it starts with factory settings (SAFE MODE).
- REC held while switching on, for 3 s: factory reset. The tilt setup is kept.
