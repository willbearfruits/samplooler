# HOWL manual

HOWL is a feedback instrument. Whatever the mic hears goes through up to eight narrow band-pass filters
(the bands), each tuned to a note of a chord, and comes out again. When the gain round that loop is above
one at a band's note, that note builds up and howls. Tipping the box sets the gain; leaning picks which
band gets the most of it.

Two loops can close:

- **inside the box** (INNER): the output goes straight back into the bands. It howls with nothing but a
  whisper of noise, so it works on headphones.
- **through the air** (AIR): the speaker plays, the mic hears it a few milliseconds later, together with the
  room's echoes. Where a howl lands depends on that delay: it sits on the nearest frequency the room lets
  through, usually a few percent from the chord's note. Move the box, cup your hand, turn to a wall: the
  howl changes.

A governor keeps every howl at HOWL LEVEL. It brings the gain down as soon as a howl passes the level and
lets it back up no faster than SWELL allows. The loop also saturates just above the level, so even a
speaker turned far too loud cannot push it over: what HOWL plays never goes more than 2 dB over HOWL
LEVEL, and never rises faster than 120 dB a second.

## The screen

The wolf is the howl: it sits when the box is quiet and throws its head back as a howl blooms, and the
howl travels to the moon, which glows with it. Each vertical string is a band, low to high from left to
right; it shivers with its level and glows when it howls. The note over the strings is the one in the
spotlight (the one leaning or the joystick has picked). Under the hill: the mode (INNER, AIR, BOTH, or
FROZEN), the chord, and the gain gauge with a tick where the inner loop starts to howl. The bottom line
is the looper.

## The menu

Press the knob for the menu, turn to choose, press to open or edit, press again to go back. Hold the knob
to go back to playing. The setting you edited last is what the knob turns while you play (a strip shows
it); choose SOUND in the menu to make the knob change sounds again.

### SOUND

| | |
| - | - |
| SOUND | the factory sounds, then your own; your last edit is kept as MY EDIT when you move on |
| SAVE AS | store the sound as USER 1..8 |
| REVERT | back to the sound as it was saved |

### CHORD

| | |
| - | - |
| ROOT | the chord's root note |
| OCTAVE | the root's octave: 5 puts it at C5 (523 Hz), where a small speaker howls best |
| CHORD | MAJOR, MINOR, SUS4, MAJ7, MIN7, DOM7, ADD9, FIFTHS, OVERTONES (harmonics 2 to 9), WHOLE TONE, DIMINISHED, HIRAJOSHI, CLUSTER |
| BANDS | how many notes can howl, 3 to 8, low to high |
| SPREAD | CLOSE: the chord stacked from the root; OPEN: every other note, wider; WIDE: from an octave down |
| DRIFT | each band wanders slowly by up to this many cents, so neighbouring howls beat |
| GLIDE | how long a band takes to slide to a new note |
| EVOLVE | SLOW (every 16 s) or FAST (every 5 s): the chord moves by a fourth, a fifth, or to its relative major or minor |
| MIDI KEYS | HOLD: notes held on a MIDI keyboard become the chord while held; LATCH: until the next chord; OFF |

### FEEDBACK

| | |
| - | - |
| MODE | INNER (the loop inside), AIR (the speaker and the room), BOTH |
| GAIN | moves the edge of howling: higher, it howls with less tilt (without a tilt sensor this is the gain) |
| RING | the bands' Q: higher is narrower and purer, lower is breathier and catches more |
| SPOTLIGHT | how much more gain the picked band gets than the others; 0: all equal |
| HOWL LEVEL | where every howl settles, -30 to -6 dB |
| SWELL | the shortest time a howl may take to grow; long makes slow blooms |
| MIC IN | how much of the mic goes into the loop |
| SEED | a little noise inside the loop so INNER can start in a silent room (lowest: off) |
| FREEZE | the mic and the seed are cut and the inner loop holds what is howling, for as long as you like |

### ROOM

| | |
| - | - |
| ROOM | a second, longer loop inside the box: a delay with dull walls. 0: only the tight loop |
| SIZE | the room's delay, 2 to 250 ms. Short rooms give comb tones, long ones echoes |
| DAMP | how dull the room's walls are |
| SHIFT | every trip round the room moves the sound up (or down) by this many Hz: howls that climb or fall for ever. Needs ROOM above 0 |
| OCTAVE GLOW | feeds a rectified copy back in: octaves above join |
| COLOUR | how the loop saturates: SOFT, WARM (lopsided, even harmonics), HARD (clean, then a short knee), FOLD (folds over: crackle and chaos) |
| HEAT | how hard the loop drives the colour; 1 is clean |
| TIDE | the gain breathes up and down by itself |
| TIDE RATE | how fast it breathes |

### MOTION

| | |
| - | - |
| TILT DOES | GAIN+NOTE (tip: gain, lean: note), GAIN, NOTE, SHIFT+NOTE (tip: the shift), OFF |
| STICK DOES | NOTE+GAIN (x: note, y: swell), SHIFT+ROOM (x: shift, y: room), OFF |
| TILT RANGE | degrees from quiet to full gain |

### LOOP, SYSTEM

The looper's level, how much each pass keeps (below 100 % the loop fades into echoes), and clearing it.
SYSTEM has the volume, mic gain, tilt setup, the joystick, MIDI, the LED, hints, the screen saver,
ABOUT and RESET ALL. These are the same on every instrument of the family.

## Tips

- Start with FIRST HOWL, tip until one note sings, then lean slowly: the howl walks up and down the chord.
- A howl that will not come: more GAIN, more RING, or a quieter room (MIC IN down) for INNER.
- AIR is the real thing, and the room decides: point the speaker at a wall, cover the mic with your hand,
  walk around. BOTH keeps the notes truer while the room still colours them.
- FREEZE a chord you like, put the box down and play over it, or catch it with REC.
- SHEPARD and FALLING sound best held still at the edge of howling.
