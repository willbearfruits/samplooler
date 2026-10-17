# JAM manual

JAM is four instruments in one box, played one at a time and looped on top of each other:

| Layer | You play it by | It comes from |
| - | - | - |
| DRUMS | knocking on the box; each side is a drum | KNOCK |
| BASS | knocking with the box tilted to a note, humming, or letting it play the chords by itself | new |
| BREATH | blowing at the mic; tilting picks the note | HUFF |
| HOWL | tilting the box into feedback tuned to the chord | HOWL |

## The knob picks the layer

Turn the knob: the next layer is live. Its scene slides in, and the mic, the tilt, the stick and REC are its.
The other layers keep playing what they looped. The header shows the four layers (the live one lit), a
square beside each one that has a loop (filled: playing, hollow: muted, tall and blinking: recording), the
chord, the four beats of the bar, and the level. The row under the header is the loop's playhead.

## Loops

- **Hold REC** while you play: when you let go it loops. The first loop of the jam, on any layer, sets the loop's
  length and the tempo: the length you held, as a whole number of beats between 75 and 165 BPM. Before that the
  tempo is SONG > TEMPO.
- Every later loop, on any layer, has that length. Hold REC again to add to the live layer's loop.
- **Tap REC** to mute or unmute the live layer's loop.
- LOOPS clears one layer, or all of them (and frees the tempo again).
- DRUMS and BASS keep their loops as notes, so their sound settings still change them afterwards; BREATH and
  HOWL are recorded as sound (up to 8 seconds).

## The song

Every layer plays in the song's key and scale, and the BASS, HOWL and the tuned drums follow its chord.
SONG > CHORDS steps the chord every bar (I V vi IV and others, or a 12-bar blues); HOLD keeps one. The BASS can
take the chord over while it is live: leaning picks it.

## Sounds

SOUND > SOUND walks the whole-jam sounds: each sets the song, the mix, the FX and all four layers.

| | |
| - | - |
| FIRST JAM | street kit, a fingered bass on I V vi IV, the ocarina, C major |
| FUNK BAND | dorian, swung boom-bap drums, a busy octave bass on the kicks, clarinet |
| DUB | slow minor, long echoes, a sub bass on quarters, flute |
| LULLABY | marimba drums, a soft sub bass, the ocarina, a big reverb |
| ACID SET | techno kit on a tight grid, an acid bass riff, a climbing howl |
| RITUAL DRONE | phrygian, hand drums, a reese bass, shakuhachi with a drone, a frozen howl |
| BLUES BAR | a 12-bar blues, swung drums, a walking bass, clarinet |
| STEEL ISLAND | steel pan drums, an octave bass, pan pipe |
| TECHNO WOLF | techno kit, an 808 bass on the kicks, a falling frozen howl |
| GLASS GARDEN | glass drums, an arpeggiated FM bass, the bottle |
| INDUSTRY | scrap metal with drive, a gritty reese bass, a hard howl |
| ANDALUSIA | harmonic minor, i VII VI V, hand drums, a walking bass, flute in thirds |
| ZAP ARCADE | laser drums, an FM arpeggio, fast echoes |

Your own: change anything, then SOUND > SAVE AS (eight user sounds).

## Controls

| | |
| - | - |
| turn the knob | the live layer |
| press the knob | the menu: SONG, each layer's two pages, MIX, FX, LOOPS, SOUND, SYSTEM |
| hold the knob | leave the menu; outside it, freeze the tilt (a padlock shows) |
| hold REC | record the live layer's loop, or add to it |
| tap REC | mute / unmute the live layer's loop |
| tilt, stick, mic | the live layer's (see each layer below) |
| REC held from power-on, 3 s | factory reset |

## The layers

**DRUMS.** Knock on the box. The face, the left and right sides, the top and bottom edges are five drums
(the KIT picks them), and each knock rings the kit's body (wood, marimba, bells, glass, tabla, steel pan,
drum skin, metal). Tuned drums and bodies BY SIDE play the chord's notes. Tilt the bottom edge down to tune
up; lean sideways to make the loop change as it plays. The stick: up rolls the last drum, down repeats the
last sixteenth, left and right filter the drums.

**BASS.** MODE picks how: KNOCK (knock to play the note the tilt points at), HUM (it follows your voice two
octaves down) or FOLLOW (it plays a bassline from the chord by itself, on the kicks with LOCK KICK). Leaning
picks the jam's chord. The stick: left/right the filter, up an octave, down an accent.

**BREATH.** Blow at the mic. Tilting walks a ladder of notes in the song's scale (bottom edge down = higher);
blowing hard jumps up. The stick bends. Six models: ocarina, flute, pan pipe, shakuhachi, clarinet, bottle.
BREATH > SETUP measures your breath: turn it to GO, stay quiet two seconds, then blow normally.

**HOWL.** Tip the bottom edge down for more gain until it howls; the howl can only be a note of the chord.
Leaning (or the stick) picks the note in the spotlight. INNER keeps the loop inside (headphones are fine),
AIR uses the speaker and the room. FREEZE keeps a howl going under the other layers.

## Every setting

The tables below come from the code (`tools/manual_tables.py`). Each setting is on its MIDI CC, both ways, on
the MIDI channel (SYSTEM > MIDI CH).

<!-- tables -->
### DRUMS

**DRUMS**

| Setting | CC | |
| - | - | - |
| KIT | 20 | Five drums, what they ring: STREET, 808, BOOM BAP, TECHNO, HAND DRUM, MARIMBA, BELLS, GLASS, TABLA, STEEL PAN, SCRAP, ZAPS, COWBELLS, SCRATCH |
| TUNE | 21 | The drums up or down (-12..12 st) |
| DECAY | 22 | Shorter or longer drums (0.25..4.0 x) |
| SENSE | 23 | How soft a knock counts (0..100 %) |
| SIDES | 24 | How a knock picks its drum: SIDE, TONE, ONE DRUM |
| GRID | 25 | What hits are pulled to: OFF, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| STRENGTH | 26 | How hard they are pulled (0..100 %) |
| SWING | 27 | Late offbeats (0..75 %) |
| MUTATE | 28 | Hits drop out or change (0..100 %) |
| UNDO LAST |  | Take away the last layer |

**DRUM KIT**

| Setting | CC | |
| - | - | - |
| BODY | 30 | How loud the ringing is (0..100 %) |
| BODY PITCH | 31 | One note, or chord by side: KIT, ONE, BY SIDE |
| RING | 32 | How long it rings (0.25..4.0 x) |
| DRIVE | 33 | Dirt (0..100 %) |
| PUNCH | 34 | Squeeze: fatter, louder (0..100 %) |
| FX SEND | 35 | Into the echo and reverb (0..100 %) |

### BASS

**BASS**

| Setting | CC | |
| - | - | - |
| MODE | 36 | Knock, hum, or it plays: KNOCK, HUM, FOLLOW |
| PATTERN | 37 | What follow plays: ROOT, ROOT+5, OCTAVES, WALK, ARP, ACID |
| RATE | 38 | How often follow plays: 1/4, 1/8, 1/16 |
| LOCK KICK | 39 | On: play with the kicks: OFF, ON |
| FILL | 40 | Extra notes in between (0..100 %) |
| OCTAVE | 41 | The whole bass up or down (-1..1) |
| LEAN CHORD | 42 | Lean to change the chord: OFF, ON |
| RIFF | 43 | The acid riff and fills (1..16) |

**BASS TONE**

| Setting | CC | |
| - | - | - |
| SOUND | 44 | The bass itself: SUB, ACID, 808, FINGER, REESE, FM |
| CUTOFF | 45 | The filter: dark to bright (60..6000 Hz) |
| RESONANCE | 46 | The filter's peak (0..100 %) |
| ENVELOPE | 47 | How far each note opens it (0..100 %) |
| DECAY | 48 | How long a note rings (0.05..2.0 s) |
| GLIDE | 49 | Slide on joined notes (0..0.5 s) |
| DRIVE | 50 | Grit (0..100 %) |
| ECHO SEND | 51 | Into the shared echo (0..100 %) |

### BREATH

**BREATH**

| Setting | CC | |
| - | - | - |
| MODEL | 52 | What you are blowing into: OCARINA, FLUTE, PAN PIPE, SHAKUHACHI, CLARINET, BOTTLE |
| OCTAVE | 53 | Shift all notes by octaves (-2..2) |
| NOTES | 54 | Notes on the tilt ladder (3..15) |
| GLIDE | 55 | Time to slide to a note (0.002..1.0 s) |
| VIBRATO | 56 | Pitch wobble, always on (0..100 %) |
| HARMONY | 57 | A second voice: OFF, 3RD UP, 5TH UP, OCTAVE UP, OCTAVE DN, DRONE |
| FX SEND | 58 | Into the echo and reverb (0..100 %) |
| HOLD | 59 | On: a drone when not live: OFF, ON |

**BLOW**

| Setting | CC | |
| - | - | - |
| SENSITIVITY | 60 | More: soft breath, loud (0..100 %) |
| GATE | 61 | More: ignore the room (0..100 %) |
| OVERBLOW AT | 62 | Jump up here. 100: never (50..100 %) |
| BREATHINESS | 63 | How much air you hear (0..100 %) |
| BRIGHTNESS | 70 | Dark to bright (0..100 %) |
| BEND RANGE | 71 | How far the stick bends (1..12 st) |
| SETUP |  | Turn: quiet 2 s, then blow: -, GO |

### HOWL

**HOWL**

| Setting | CC | |
| - | - | - |
| MODE | 74 | Air needs the speaker: INNER, AIR, BOTH |
| GAIN | 75 | Moves the howling edge (0..100 %) |
| RING | 76 | How narrow each note is (6..150) |
| HOWL LEVEL | 77 | The loudest a howl gets (-30..-6 dB) |
| FREEZE | 78 | Howls on under the others: OFF, ON |
| OCTAVE | 79 | Where the howls sit (3..6) |
| BANDS | 80 | How many notes can howl (3..8) |
| GLIDE | 81 | Slide to a new chord (0.02..4.0 s) |

**HOWL ROOM**

| Setting | CC | |
| - | - | - |
| ROOM | 82 | A room inside the loop (0..100 %) |
| SIZE | 83 | The inner room's delay (0.002..0.25 s) |
| SHIFT | 84 | Howls climb or fall (-20..20 Hz) |
| COLOUR | 85 | How the loop saturates: SOFT, WARM, HARD, FOLD |
| HEAT | 86 | How hard the colour bites (1..10 x) |
| DRIFT | 87 | Slow detune: howls beat (0..50 ct) |
| SWELL | 88 | How slowly howls grow (0.2..10.0 s) |
| SEND | 89 | Into the echo and reverb (0..100 %) |

### SONG, MIX, FX, LOOPS

**SONG**

| Setting | CC | |
| - | - | - |
| LAYER | 90 | The layer you play: DRUMS, BASS, BREATH, HOWL |
| TEMPO | 91 | Till the first loop sets it (60..180 bpm) |
| KEY | 92 | The key of every layer: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE | 93 | The notes every layer uses: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| CHORDS | 94 | How the chords move by bar: HOLD, I V vi IV, vi IV I V, I vi IV V, I IV, ii V I, i VII VI V, i iv, 12 BAR BLUES |
| HOLD CHORD | 95 | The chord for chords: hold: I, II, III, IV, V, VI, VII |
| CHORD BARS | 96 | How long each chord lasts: 1 BAR, 2 BARS, HALF BAR |

**MIX**

| Setting | CC | |
| - | - | - |
| DRUMS | 97 | Drums level (0..100 %) |
| BASS | 98 | Bass level (0..100 %) |
| BREATH | 99 | Breath level (0..100 %) |
| HOWL | 100 | Howl level (0..100 %) |
| MUTE DRUMS | 101 | Silence the drums: PLAY, MUTE |
| MUTE BASS | 102 | Silence the bass: PLAY, MUTE |
| MUTE BREATH | 103 | Silence the breath: PLAY, MUTE |
| MUTE HOWL | 104 | Silence the howl: PLAY, MUTE |

**FX**

| Setting | CC | |
| - | - | - |
| ECHO | 105 | How loud the echo is (0..100 %) |
| ECHO TIME | 106 | In time with the song: 1/8, 1/8 DOT, 1/4, 1/4 DOT, 1/2 |
| REPEATS | 107 | How long the echo goes on (0..90 %) |
| REVERB | 108 | How loud the reverb is (0..100 %) |
| REVERB TIME | 109 | How long the room rings (0.3..6.0 s) |
| REVERB TONE | 110 | Dark to bright (0..100 %) |

**LOOPS**

| Setting | CC | |
| - | - | - |
| CLEAR DRUMS |  | Erase the drums' loop |
| CLEAR BASS |  | Erase the bass's loop |
| CLEAR BREATH |  | Erase the breath track |
| CLEAR HOWL |  | Erase the howl track |
| CLEAR ALL |  | Every loop; a new tempo |
<!-- /tables -->

## MIDI

| | |
| - | - |
| out | DRUMS on channel 10 (General MIDI drum notes), BREATH on the MIDI channel, BASS on the next, HOWL on the one after; settings as CC when changed on the box; program change when the sound changes |
| in | on the MIDI channel: notes and CCs go to the live layer; CC 20+ set settings; program change picks a sound; note 60 is REC |

## When something is wrong

- Knocks are missed or it plays by itself: DRUMS > SENSE, SYSTEM > MIC GAIN.
- Breath is not heard, or the room sets it off: BREATH > SETUP.
- HOWL in AIR needs the speaker; on headphones use INNER.
- After a restart that was not clean the screen shows CRASH, BROWNOUT or WATCHDOG for a moment; three in a
  row start it with factory settings.
