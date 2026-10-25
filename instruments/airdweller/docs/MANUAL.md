# AIRDWELLER manual

AIRDWELLER is one instrument in two boxes, one in each hand. Each box has a role. A DRUM box is swung like a stick
and plays synthesized drums on the beat. A PAINT box rings a material, struck by the other hand's hits and shaped by
its own hand. A PUMP box bows the same material: moving pumps it in, holding still lets it fall. One box alone works
too, with a mixed kit.

## Two hands

Turn both boxes on. They find each other over the radio in a few seconds ("THE OTHER HAND IS HERE"), share one
tempo and one key, and settle which hand each is: with HAND on AUTO the box with the lower radio id is the LEFT hand.
Set HAND to LEFT or RIGHT on each box to choose. The usual pairing is a DRUM box in one hand and a PAINT or PUMP box
in the other, but any two roles work: two DRUM boxes are a full kit split across the hands, two PUMP boxes a drone
you work with both arms.

## DRUM: the strokes

Swing the box like a drumstick: a short fast turn that stops. Forward, left, right and back are four pads. The hand
coming back after a stroke is not a hit, and a slow turn is not a hit. With CONFIRM on, a hit also needs the jolt of a
stick stopping, so turning the box quickly in the air does nothing. SENSE sets how easily a swing counts; FORWARD and
SIDES turn the directions round for the way you hold it.

Each hit lands on the beat grid (QUANTIZE: 1/16, 1/8 or OFF). A hit just before a line waits for it; a hit just
after plays at once. The harder the swing, the louder the hit.

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

## Loops

Hold REC and play: a loop of whole bars, recorded from the next bar line (or the one just passed). Let go and it
closes on the nearest bar and plays at once. There are four loops, stacked. A fifth recording replaces the newest.
The stick toggles them: up loop 1, right loop 2, down loop 3, left loop 4. Hold the knob: every loop stops; hold it
again: they go. LOOPS > ERASE LAST and ERASE ALL remove them.

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
| ROLE | 20 | Drum, paint or pump: DRUM, PAINT, PUMP |
| HAND | 21 | Which hand this box is: AUTO, LEFT, RIGHT |
| PAIR | 22 | The other hand strikes it: OFF, ON |
| DUCK | 23 | Its hits push this down (0..100 %) |
| LEVEL | 24 | How loud (0..100 %) |

### KIT

| Setting | CC | |
| - | - | - |
| FRONT | 25 | Swung forward |
| LEFT | 26 | Swung left |
| RIGHT | 27 | Swung right |
| BACK | 28 | Swung back |
| TUNE | 29 | The drums up or down (-12..12 st) |
| DECAY | 30 | Shorter or longer (0.25..4.0 x) |
| SQUEEZE | 31 | Folded, plastic, harder (0..100 %) |

### MATERIAL

| Setting | CC | |
| - | - | - |
| MATERIAL | 32 | What rings |
| TILT MORPH | 33 | Tilt it: another material (0..100 %) |
| TUNE TO | 34 | It rings with the kick: THE KICK, THE KEY |
| RATIO | 35 | Over the kick's note: X1, X2, X3, X4, X6, X8 |
| CHORD | 36 | The notes it rings on: ONE, FIFTH, OCTAVE, TRIAD 7 |
| RING | 37 | How long a strike rings (0.1..12.0 s) |
| BRIGHT | 38 | Its upper modes (0..100 %) |
| TWIST BEND | 39 | Twist it: the pitch bends (0..24 st) |
| CREAK | 40 | Shake it: it scrapes (0..100 %) |

### ENERGY

| Setting | CC | |
| - | - | - |
| CLIMB | 41 | Moving this long: full (1.0..60.0 s) |
| FALL | 42 | Still this long: back down (2.0..120.0 s) |
| REST LEVEL | 43 | Pump: how quiet unmoved (-40..0 dB) |
| SHAKE | 44 | What a shake throws in (0..100 %) |

### LOOPS

| Setting | CC | |
| - | - | - |
| TEMPO | 45 | Shared with linked boards (50..200) |
| LOOP 1 |  | On or off: EMPTY, OFF, ON |
| LOOP 2 |  | On or off: EMPTY, OFF, ON |
| LOOP 3 |  | On or off: EMPTY, OFF, ON |
| LOOP 4 |  | On or off: EMPTY, OFF, ON |
| ERASE LAST |  | The newest loop goes |
| ERASE ALL |  | Every loop goes |
| QUANTIZE | 52 | Hits land on this grid: OFF, 1/8, 1/16 |
| SWING | 53 | The off sixteenths late (0..75 %) |

### FX

| Setting | CC | |
| - | - | - |
| DRIVE | 54 | Overdrive on everything (0..100 %) |
| TONE | 55 | Dark to bright (0..100 %) |
| ECHO | 56 | The echo's time (0.02..1.6 s) |
| ECHO SYNC | 57 | On the beat: OFF, 1 BEAT, 3/4, 1/2, 1/4 |
| FEEDBACK | 58 | How often it comes back (0..95 %) |
| ECHO MIX | 59 | Dry to all echo (0..100 %) |
| ROOM | 60 | How much room (0..100 %) |
| ROOM SIZE | 61 | A booth to a hangar (0..100 %) |

### FEEL

| Setting | CC | |
| - | - | - |
| SENSE | 62 | How easily a swing counts (0..100 %) |
| CONFIRM |  | A hit needs a jolt too: OFF, ON |
| FORWARD |  | Which way is front: TIP AWAY, TIP TO ME |
| SIDES |  | Which side is left: KICK LEFT, KICK RIGHT |

### SONG

| Setting | CC | |
| - | - | - |
| KEY | 72 | The kick's note: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

## MIDI

- In: CC 20 and up sets a setting, program change picks a sound.
- Out: each hit that sounds as a note on channel 10 (General MIDI drum notes: KICK 36, SNAP 38, CLANG 49, HAT 42, GLASS
  81, SUB 35, WHIP 39, CREAK 54), each setting as it changes.

## Levels

To be measured on the engine's final sound.
