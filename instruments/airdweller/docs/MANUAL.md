# AIRDWELLER manual

AIRDWELLER is one instrument in two boxes, one in each hand. Each box has a role. A DRUM box is swung like a stick
and plays synthesized drums on the beat. A NOTES box draws basslines and leads: the tilt picks the note. A PAINT box
rings a material, struck by the other hand's hits and shaped by its own hand. A PUMP box bows the same material:
moving pumps it in, holding still lets it fall. A SHAPE box plays nothing itself: it drags the other hand's sounds.
After any hit, moving on slowly drags the sound you just made. One box alone works too, with a mixed kit.

## Two hands

Turn both boxes on. They find each other over the radio in a few seconds ("THE OTHER HAND IS HERE"), share one
tempo and one key, and settle which hand each is: with HAND on AUTO the box with the lower radio id is the LEFT hand.
Set HAND to LEFT or RIGHT on each box to choose. Some pairings:

- DRUM in one hand, PAINT in the other: the kick strikes a metal that rings on the kick's note.
- DRUM and SHAPE: one hand hits, the other pulls the sound it made.
- DRUM and NOTES: a beat in one hand, a bassline in the other, on the same grid and in the same key.
- Two DRUM boxes: a full kit split across the hands. Two PUMP boxes: a drone you work with both arms.

## DRUM: the strokes

Swing the box like a drumstick: a short fast turn that stops, as a stick stops on a drum. The stop is what makes a
hit. The hand coming back, a sweep that slows down by itself, a slow turn and a twist of the wrist are not hits.

The four pads are directions in the room, measured against gravity, so the angle you hold the box at does not matter:

- **Forward**: the front edge tipped down. **Back**: tipped up.
- **Left** and **Right**: swept round, as if hitting a drum to that side.
- A twist of the wrist (a roll about your forearm) is never a pad. It bends the pitch instead.

The box reads its motion a thousand times a second, so hits can come fast: ten kicks a second, or a hat and a snare
80 ms apart. Shake the box (back and forth, smoothly) and the last pad rolls, one hit to each sixteenth.

STOP sets how sharp a stop has to be: higher is stricter, with fewer false hits. SENSE sets how fast a swing has to
be. With CONFIRM on, a hit also needs the jolt of the stop. FORWARD and SIDES turn the directions round.

Each hit lands on the beat grid (QUANTIZE: 1/16, 1/8 or OFF). A hit stopped just before a line waits for it; one just
after plays at once. With QUANTIZE OFF a hit sounds 12 ms after the stop, always the same. The harder the swing, the
louder the hit.

## The drag

After a hit, keep moving slowly and you drag the sound you just made. A quick, natural return does nothing; a slow
pull does. How far you pull sets how much (REACH: this many degrees is the most), and the sound holds while you keep
moving. Twisting the wrist while you drag bends it (DRAG BEND). What a drag does depends on the sound:

- The kick bends down, up to an octave, and stretches. The sub slides further.
- The snare's noise hangs on and darkens. The hat opens.
- Metal and glass keep ringing and darken.
- The whip sweeps up again. The creak keeps creaking.
- A note holds and slides (see NOTES).

DRAG sets how much any of this happens; at 0 the drag does nothing.

## The kits

The pads take their voices from the hand (each pad on AUTO), or from KIT > FRONT, LEFT, RIGHT and BACK.

| Pad | LEFT hand | RIGHT hand | One box alone |
| - | - | - | - |
| Forward | WHIP | HAT | HAT |
| Left | KICK | CLANG | KICK |
| Right | SNAP | GLASS | SNAP |
| Back | SUB | CREAK | CLANG |

The voices are made from nothing, no samples:

- **KICK**: a click that drops to the sub in a few milliseconds, folded and squeezed, with a long tail.
- **SNAP**: a resonant band pass whipped down over a burst of noise and a pitched pop.
- **CLANG**: a struck plate, its modes out of tune with each other, bright at the start, ringing on.
- **HAT**: a high cluster of metal modes and filtered noise, short.
- **GLASS**: an FM bell, high, its brightness falling fast.
- **SUB**: a sine that falls an octave or more over a second.
- **WHIP**: a band pass swept up through noise in a few milliseconds.
- **CREAK**: friction, a jittered train of pulses through metal modes.

The kick sits on the KEY, between F1 and E2; TUNE moves the whole kit. DECAY makes every voice shorter or longer.
SQUEEZE folds and drives the drums harder, towards plastic.

## PAINT: a material that rings

A PAINT box holds a material: METAL, GLASS, RUBBER or LATEX. The other hand's hits strike it at the same instant they
sound, on the shared clock: a kick strikes it heavy and low, a hat light and high, a snap or a whip bright, a creak
scrapes it. The box's own swings strike it too. While it rings, the hand shapes it:

- **Tilt** it away from you or towards you: it moves from MATERIAL to the next ones (TILT MORPH sets how far).
- **Twist** it: the pitch bends, up to TWIST BEND semitones, and comes back when you stop.
- **Roll** it right: brighter; left: darker, around BRIGHT.
- **Moving** it bows it, so it sustains between strikes.
- **Shaking** it scrapes it (CREAK).

TUNE TO sets its pitch. THE KICK puts it on the other hand's kick (or this box's own) times RATIO, so the ring and the
kick agree; THE KEY puts it on the key's note. CHORD rings it on one note, a fifth, a fifth and an octave, or four
notes (1/1, 5/4, 3/2, 7/4). RING is how long a strike rings.

## PUMP: the material bowed

A PUMP box bows the material instead of striking it. Swings are not hits here: all movement is energy. Moving pumps
energy in (CLIMB: how long it takes to fill), holding still lets it fall (FALL), and a shake throws in extra (SHAKE).
At no energy the material sits at REST LEVEL; at full energy it is loud and bright. With PAIR on, each of the other
hand's hits ducks the drone (DUCK), so the two interlock.

## NOTES: the tilt is the note

A NOTES box plays a scale (KEY and SCALE, shared with the other box) over SPAN notes, from OCTAVE up. Tip the front
edge down for the lowest note and up for the highest; level is the middle. Roll the box past 35 degrees to the right
for an octave up, to the left for an octave down. Each note has its own stretch of the tilt, and the box holds a note
a little past its edge, so it does not flicker between two.

A hit plays the note the tilt pointed at when the stroke began. On its own it is short (NOTE LENGTH). If you keep
moving slowly after the hit, the note holds, and it slides to whatever note the tilt points at (GLIDE). Hold still
and it ends. A shake repeats the note on the sixteenths.

VOICE picks the sound: BASS (a rubber bass), SQUELCH (an acid line), LEAD (a latex lead with vibrato), BELL (glass,
up to four ringing). Notes go out on MIDI on the SYSTEM channel.

## SHAPE: the other hand drags

A SHAPE box makes no sound. When the other box hits, this hand's slow movement drags that hit, just as the other hand
could drag it itself: how far you pull, whether you keep moving, the twist. The other box takes the larger of its own
drag and this one. A new hit on the other box starts a new drag.

## EASY

With EASY on, the direction does not matter: up, down, left, right, forward, back or a twist all count.

- **STEP**: each swing plays the next step of PATTERN on the next free sixteenth. Shake the box to play a beat; a
  swing faster than the grid is let go. In NOTES the pattern is notes of the scale from where the tilt points.
- **GROOVE**: the pattern plays by itself while you move, and stops a bar after you stop.

The patterns are FOUR, BACKBEAT, BREAK, HALF, BOUNCE and ROLL. Changing the pattern starts it from its first step.

## Loops

Hold REC and play: a loop of whole bars, recorded from the next bar line (or the one just passed). Let go and it
closes on the nearest bar and plays at once. There are four loops, stacked. A fifth recording replaces the newest.
The stick toggles them: up loop 1, right loop 2, down loop 3, left loop 4. Hold the knob: every loop stops; hold it
again: they go. LOOPS > ERASE LAST and ERASE ALL remove them. A NOTES box's loops keep its notes and how long each
was held.

A DRUM box sends its loops' hits to the other box a quarter of a beat before they are due, so a looped kick still
strikes the other hand's material on time.

## CUT

Tap REC: a hard silence at once, the way SOPHIE's tracks stop dead. The loops keep time underneath. Tap again: the
sound comes back on the next beat.

## The link

The boxes use the instruments' link (ESP-NOW, channel 1): one tempo, one key, the same bar lines. TEMPO is shared;
while a loop plays anywhere, that loop keeps the tempo. LINK turns the radio off, GROUP keeps several pairs apart,
RANGE sets how far it reaches, WHO IS THERE lists the boards heard. Hits go across with their beat position, so both
boxes sound them on the same sample when the radio gets there first; a live hit placed less than about 10 ms ahead
reaches the other box a little late.

## Sounds

Each sound is a kit, a material and a room, for any role:

| Sound | |
| - | - |
| FACESHOP | squeezed kit, metal on a fifth, short room |
| PONY | the hardest squeeze and drive, latex, wide bends, a lot of creak |
| LATEX | rubbery latex, one note, big bends |
| STEEL | metal on a fifth and an octave, long, a bigger room |
| GLASSHOUSE | glass high above the kick, long, a big room and an echo |
| RUBBER ROOM | rubber, dark, squeezed and driven |
| MECHANICAL ANIMAL | metal on four notes, all creak |
| SILICONE | latex towards glass, long bends, a synced echo |
| SUB PRESSURE | long kicks, rubber low down, a deep rest level |
| CHAIN PUMP | metal, long ring, creak, made for PUMP |
| HARD STOPS | short, dry, bright: for CUT |

## Settings

Every setting before the device's own is on MIDI CC 20 and up, in this order.

<!-- tables -->
### PLAY

| Setting | CC | |
| - | - | - |
| ROLE | 20 | What this hand does |
| EASY | 21 | Any swing: the next step: OFF, STEP, GROOVE |
| PATTERN | 22 | What easy plays: FOUR, BACKBEAT, BREAK, HALF, BOUNCE, ROLL |
| HAND | 23 | Which hand this box is: AUTO, LEFT, RIGHT |
| PAIR | 24 | The other hand strikes it: OFF, ON |
| DUCK | 25 | Its hits push this down (0..100 %) |
| LEVEL | 26 | How loud (0..100 %) |

### KIT

| Setting | CC | |
| - | - | - |
| FRONT | 27 | Swung forward |
| LEFT | 28 | Swung left |
| RIGHT | 29 | Swung right |
| BACK | 30 | Swung back |
| TUNE | 31 | The drums up or down (-12..12 st) |
| DECAY | 32 | Shorter or longer (0.25..4.0 x) |
| SQUEEZE | 33 | Folded, plastic, harder (0..100 %) |

### DRAG

| Setting | CC | |
| - | - | - |
| DRAG | 34 | After a hit, move: it bends (0..100 %) |
| REACH | 35 | This far: a full drag (15..180 deg) |
| DRAG BEND | 36 | Twist it while dragging (0..24 st) |

### NOTES

| Setting | CC | |
| - | - | - |
| VOICE | 37 | What the notes are |
| SCALE | 38 | Shared with linked boards: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| OCTAVE | 39 | The lowest note's octave (1..5) |
| SPAN | 40 | Notes over the tilt (3..15) |
| NOTE LENGTH | 41 | A flick: this long (0.04..1.0 s) |
| GLIDE | 42 | From note to note (0.01..0.5 s) |

### MATERIAL

| Setting | CC | |
| - | - | - |
| MATERIAL | 43 | What rings |
| TILT MORPH | 44 | Tilt it: another material (0..100 %) |
| TUNE TO | 45 | It rings with the kick: THE KICK, THE KEY |
| RATIO | 46 | Over the kick's note: X1, X2, X3, X4, X6, X8 |
| CHORD | 47 | The notes it rings on: ONE, FIFTH, OCTAVE, TRIAD 7 |
| RING | 48 | How long a strike rings (0.1..12.0 s) |
| BRIGHT | 49 | Its upper modes (0..100 %) |
| TWIST BEND | 50 | Twist it: the pitch bends (0..24 st) |
| CREAK | 51 | Shake it: it scrapes (0..100 %) |

### ENERGY

| Setting | CC | |
| - | - | - |
| CLIMB | 52 | Moving this long: full (1.0..60.0 s) |
| FALL | 53 | Still this long: back down (2.0..120.0 s) |
| REST LEVEL | 54 | Pump: how quiet unmoved (-40..0 dB) |
| SHAKE | 55 | What a shake throws in (0..100 %) |

### LOOPS

| Setting | CC | |
| - | - | - |
| TEMPO | 56 | Shared with linked boards (50..200) |
| LOOP 1 |  | On or off: EMPTY, OFF, ON |
| LOOP 2 |  | On or off: EMPTY, OFF, ON |
| LOOP 3 |  | On or off: EMPTY, OFF, ON |
| LOOP 4 |  | On or off: EMPTY, OFF, ON |
| ERASE LAST |  | The newest loop goes |
| ERASE ALL |  | Every loop goes |
| QUANTIZE | 63 | Hits land on this grid: OFF, 1/8, 1/16 |
| SWING | 70 | The off sixteenths late (0..75 %) |

### FX

| Setting | CC | |
| - | - | - |
| DRIVE | 71 | Overdrive on everything (0..100 %) |
| TONE | 72 | Dark to bright (0..100 %) |
| ECHO | 73 | The echo's time (0.02..1.6 s) |
| ECHO SYNC | 74 | On the beat: OFF, 1 BEAT, 3/4, 1/2, 1/4 |
| FEEDBACK | 75 | How often it comes back (0..95 %) |
| ECHO MIX | 76 | Dry to all echo (0..100 %) |
| ROOM | 77 | How much room (0..100 %) |
| ROOM SIZE | 78 | A booth to a hangar (0..100 %) |

### FEEL

| Setting | CC | |
| - | - | - |
| SENSE | 79 | How easily a swing counts (0..100 %) |
| STOP | 80 | How sharp a stop is a hit (0..100 %) |
| CONFIRM |  | A hit needs a jolt too: OFF, ON |
| FORWARD |  | Which way is front: TIP AWAY, TIP TO ME |
| SIDES |  | Which side is left: KICK LEFT, KICK RIGHT |

### SONG

| Setting | CC | |
| - | - | - |
| KEY | 84 | The kick's note: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

## MIDI

- In: CC 20 and up sets a setting, program change picks a sound.
- Out: each hit that sounds as a note on channel 10 (General MIDI drum notes: KICK 36, SNAP 38, CLANG 49, HAT 42, GLASS
  81, SUB 35, WHIP 39, CREAK 54); NOTES on the SYSTEM channel; each setting as it changes.

## Recording your gestures

The detector is tuned on recordings of real playing. With the board on USB, `tools/motion_rec.py guided --instrument
airdweller` walks through a list of gestures (ten swings each way, fast ones, drags, returns, twists, a shake) and
records each one at 1 kHz. The takes go to the lab folder, and the simulator plays every one back through the
detector and checks it against what it was meant to be.

```sh
python3 tools/motion_rec.py list
python3 tools/motion_rec.py guided --instrument airdweller
python3 tools/motion_rec.py show <take>
```

## Levels

Measured in the simulator with the default bus. The drums peak between -9 and -3 dBFS at full velocity: the KICK and
SUB loudest, the HAT quietest. A swing at about a third of full strength is some 17 dB down. SQUEEZE at 100 stays under
-1 dBFS. A heavy strike on the material peaks near -12 dBFS. A PUMP drone moved to full energy is about 37 dB over the
same drone at rest with REST LEVEL at -24 dB. The kick sits between 44 and 82 Hz: the small speaker loses most of it,
the line out has it.
