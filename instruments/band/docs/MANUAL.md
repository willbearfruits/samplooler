# BAND manual

BAND is six instruments in one box, played one at a time and looped or left playing on top of each other:

| Layer | You play it by | It comes from |
| - | - | - |
| DRUMS | drawing a beat on a 4 x 8 grid, or knocking on the box (each side is a drum) | KNOCK, and new |
| BASS | writing a line with the knob, knocking with the box tilted to a note, humming, or letting it play the chords by itself | JAM |
| LEAD | letting it arpeggiate the chords, humming (it follows you), or tilting to a note and knocking | new |
| KEYS | letting it play the chords in a rhythm, or knocking for a chord | new |
| BREATH | blowing at the mic; tilting picks the note | HUFF |
| HOWL | tilting the box into feedback tuned to the chord | HOWL |

## The knob picks the layer

Turn the knob: the next layer is live. Its scene slides in, and the mic, the tilt, the stick and REC are its.
The others keep playing their loops, and the ones that play by themselves (the grid, the bass's FOLLOW, the keys'
COMP, the lead's ARP, once each has been live for a second) keep playing. The header shows the six layers (the
live one lit), a line under each one that loops (solid: playing, dotted: muted, a blinking bar: recording; a dot:
it plays by itself), the chord, the four beats of the bar, the link and the level. The row under the header is the
loop's playhead. One more turn past HOWL is the mixer (below).

## The grid

Hold the knob while DRUMS is live (or DRUMS > EDIT GRID). Four rows (the kit's kick, snare, hat and tom, or for a
tonal kit four notes of the chord) by eight steps:

| | |
| - | - |
| turn | move through the 32 steps, row by row (a fast turn moves 2) |
| press | the step: off, on, accent, off |
| press and turn | paint: every step you pass takes what the first one got |
| hold | close the grid (the knob picks layers again) |
| REC tap | start / stop the beat |
| (knocks) | not heard while the grid is open: pressing the knob jolts the box |
| REC held 1 s, 2 s | clear the row, the whole grid |
| stick | up and down rows, left and right steps; its click is REC |

Outside the grid, a tap on REC with DRUMS live stops what the drums play (the grid and the knocked loop); the next
tap brings back what was stopped. DRUMS > KNOCKS: OFF and knocks on the box play nothing (the grid alone; the knocked
loop still plays). GRID STEP: 1/8 (the eight steps are a bar), 1/16 (half a bar, twice), 1/4 (two bars). GRID BEAT loads a beat:
ROCK, FUNK, HIP HOP, FOUR FLOOR, ONE DROP, HALF TIME, TRAP; drawing makes it MINE. SWING and MUTATE act on it. The
grid's kicks are the ones the bass locks to (LOCK KICK) when there is no knocked loop.

## The bass line

Hold the knob while BASS is live (or BASS > EDIT LINE). MODE becomes LINE and the line plays while you write it.
Sixteen steps across, the notes of the scale up and down, counted from the chord's root (the root rows are marked,
the third and fifth lighter), so the line follows the chords. The rows scroll with the cursor.

| | |
| - | - |
| turn | move along the steps |
| press and turn | the step's note up or down the scale (you hear each one) |
| press | the step: a note, a held note (it rings to the next one), off |
| hold | close the editor (the knob picks layers again) |
| REC tap | stop / start the bass |
| REC held 1 s | clear the line |
| stick | up and down the note, left and right the step |

LINE loads a ready line: ROOT EIGHTHS, OCTAVE POP, WALKING, ROOT-FIFTH, FUNK, ONE DROP, ACID SLIDE (these have
accents and slides too); writing makes it MINE. LINE STEP: 1/16 (the 16 steps are a bar) or 1/8 (two bars). After a
second with BASS live the line keeps playing under the other layers, as FOLLOW does. Notes recorded into the bass's
loop play over it on their sixteenths.

## Loops

- **Hold REC** while a layer plays: it records into the loop. A first take starts at its first sound (the first
  knock, note or chord), so the silence before it is cut; REC's own click is never a knock in the loop (knocks from
  just before a press or release to 0.12 s after it are dropped). The first loop (on any layer, or on a linked board)
  sets the loop's length: whole bars at the tempo, as many as you played, rounded; let go early and it records on to
  the end of the bar. With SONG > FIRST LOOP: SETS TEMPO and nothing playing in time, the length you held sets the
  tempo instead, as JAM did.
- Every later loop, on any layer, has that length and starts on the same beat. Hold REC again to add.
- A loop plays every pass until you stop it. **Tap REC** to mute or unmute the live layer's loop (on DRUMS: stop or
  start the drums, grid and loop together; on BASS, KEYS and LEAD it also stops their playing by themselves).
- **To redo a layer**: tap REC to stop it, then hold REC. The new take replaces the old one (BREATH and HOWL write
  over their track for one pass; you hear only what you play now). A tap instead brings the old one back.
- The mixer (below) erases a layer's loop, or every loop; so does LOOPS in the menu (clearing all frees the tempo).
- BREATH and HOWL fall silent when you move to another layer, unless BREATH > HOLD or HOWL > FREEZE is on (the
  sounds RITUAL DRONE and TECHNO WOLF turn them on); their loops are the only thing that keeps playing.
- DRUMS, BASS, LEAD and KEYS keep their loops as notes, so their sound settings still change them; the lead and
  the keys keep their notes as steps from the chord, so a loop follows the chords. BREATH and HOWL are recorded as
  sound (up to 8 seconds).

## The mixer

Turn the knob past HOWL. Six faders, one per layer, each with its meter, its level and its loop (a bar: playing;
dotted: muted; a dot: it plays by itself). No layer is live here: the mic and the tilt do nothing.

| | |
| - | - |
| stick left / right | pick a layer |
| stick up / down | its level (held: keeps going) |
| tap REC | mute / unmute it |
| hold REC 1 s | erase its loop (a line fills under its icon first) |
| hold REC 2 s | erase every loop, and free the tempo |
| hold the knob | the knob works the mixer: turn picks, press and turn sets the level, tap mutes, hold gives it back |
| turn the knob | back to the layers |

The levels and mutes are MIX in the menu: they are saved with a sound.

## The song

Every layer plays in the song's key and scale, and the BASS, LEAD, KEYS, HOWL and the tuned drums follow its chord.
SONG > CHORDS steps the chord every bar (I V vi IV and others, or a 12-bar blues); HOLD keeps one. The BASS can take
the chord over while it is live: leaning picks it. The chords count bars within the loop, so a loop always meets
the same chords.

## The link

BAND and SING (or any board on the link) keep one clock and share the tempo, the bar, the loop's length and the
song's key, scale and chords over ESP-NOW (the Wi-Fi radio, no router). BAND is the clock. LINK: on or off; GROUP:
only boards in the same group link; RANGE: NEAR (2 dBm), ROOM (8), FAR (14) (every packet is a burst of current
from the DAC's supply: if you hear a faint ticking ten times a second, try NEAR); WHO IS THERE lists the boards.
A board switched on late joins the tempo that is playing; a board that already has a loop keeps it.

## Sounds

SOUND > SOUND walks the whole-band sounds: each sets the song, the grid, the mix, the FX and all six layers.

| | |
| - | - |
| FIRST BAND | C major, I V vi IV, a rock beat on the grid (off until you open it), every layer at its default |
| ROCK BAND | the rock beat on, a fingered bass, piano quarters, a brass arpeggio |
| FUNK BAND | dorian, the funk beat on sixteenths, an octave bass on the kicks, clav offbeats, a sync lead riff |
| HIP HOP | the hip hop beat swung, a bass on the kicks, electric piano sevenths, a whistle you tilt |
| DUB | one drop, a sub bass, the organ's skank, long echoes |
| LULLABY | marimba knocks, a soft sub, piano arpeggios with the pedal, a whistle |
| ACID SET | four on the floor, an acid bass riff, a saw arpeggio, a pad under it |
| RITUAL DRONE | phrygian, hand drums, a reese bass, shakuhachi with a drone, a frozen howl |
| BLUES BAR | a 12-bar blues, half time swung, a walking bass, gospel piano, brass |
| STEEL ISLAND | steel pan, an octave bass, vibes offbeats, a chip arpeggio |
| TECHNO WOLF | four on the floor, an 808 bass on the kicks, a supersaw in triplets, a falling frozen howl |
| TRAP | the trap beat, a long 808, an FM bell riff, a pad |
| GLASS GARDEN | glass drums, an arpeggiated FM bass, vibes, a random bell |
| INDUSTRY | scrap metal on the rock beat, a gritty reese, a driven clav and square |
| ANDALUSIA | harmonic minor, i VII VI V, hand drums, a walking bass, piano bossa, flute in thirds |
| ZAP ARCADE | laser drums, an FM arpeggio bass, clav stabs, a chip lead |

Your own: change anything, then SOUND > SAVE AS (eight user sounds, the grid with them).

## Controls

| | |
| - | - |
| turn the knob | the live layer; past HOWL, the mixer |
| press the knob | the menu: SONG, each layer's two pages, MIX, FX, LOOPS, LINK, SOUND, SYSTEM |
| hold the knob | on DRUMS the grid, on BASS the line, on MIX the mixer's knob; elsewhere freeze the tilt (a padlock shows); in the menu leave it |
| hold REC | record the live layer's loop, or add to it; stopped (tapped) first: a new take in its place |
| tap REC | mute / unmute the live layer's loop |
| tilt, stick, mic | the live layer's (see each layer below) |
| REC held from power-on, 3 s | factory reset |

## The layers

**DRUMS.** The grid (above), and knocks: the face, the left and right sides, the top and bottom edges are five
drums (the KIT picks them), and each knock rings the kit's body (wood, marimba, bells, glass, tabla, steel pan, drum
skin, metal). Tuned drums and bodies BY SIDE play the chord's notes. Tilt the bottom edge down to tune up; lean
sideways to make the loop change as it plays. The stick: up rolls the last drum, down repeats the last sixteenth,
left and right filter the drums.

**BASS.** MODE picks how: KNOCK (knock to play the note the tilt points at), HUM (it follows your voice two octaves
down), FOLLOW (a bassline from the chord by itself, on the kicks with LOCK KICK) or LINE (the line you write, above). Leaning picks the band's chord.
The stick: left/right the filter, up an octave, down an accent.

**LEAD.** MODE: ARP (it breaks the chord up on the grid: UP, DOWN, UP-DOWN, RANDOM, OCTAVES, two riffs; tipping the
bottom edge down climbs octaves, leaning thins or fills it), HUM (it follows your voice, pulled to the scale by
SNAP), TILT (leaning picks a note of the scale around the chord, a knock plays it). Eight sounds: SAW, SQUARE, SYNC,
SUPERSAW, FM BELL, CHIP, WHISTLE, BRASS, each with its COLOUR; a filter with its envelope, glide, vibrato, drive.
The stick: X the filter, up an octave, down a bend.

**KEYS.** MODE: COMP (it plays the chord in a PATTERN: WHOLE, HALF, QUARTERS, OFFBEAT, STABS, ARPEGGIO, GOSPEL,
BOSSA, each chord voiced nearest the last) or KNOCK (a knock plays the chord). VOICING: CLOSE, OPEN, SEVENTHS,
SPREAD. Six sounds: PIANO, E.PIANO, ORGAN (with its rotor), PAD, CLAV, VIBES. Tipping the bottom edge down moves the
chord up an inversion at a time; leaning right opens the tone. The stick: up holds the pedal, down plays short,
right more MOTION, left grit.

**BREATH.** Blow at the mic. Tilting walks a ladder of notes in the song's scale (bottom edge down = higher);
blowing hard jumps up. The stick bends. Six models: ocarina, flute, pan pipe, shakuhachi, clarinet, bottle.
BREATH > SETUP measures your breath: turn it to GO, stay quiet two seconds, then blow normally.

**HOWL.** Tip the bottom edge down for more gain until it howls; the howl can only be a note of the chord.
Leaning (or the stick) picks the note in the spotlight. INNER keeps the loop inside (headphones are fine), AIR uses
the speaker and the room. FREEZE keeps a howl going under the other layers.

## Every setting

The tables below come from the code (`tools/manual_tables.py`). The settings with a CC number are on that MIDI CC,
both ways, on the MIDI channel (SYSTEM > MIDI CH); there are more settings than CCs, so BREATH, HOWL and the
song's have none.

<!-- tables -->
### DRUMS

**DRUMS**

| Setting | CC | |
| - | - | - |
| KIT | 20 | Five drums, what they ring: STREET, 808, BOOM BAP, TECHNO, HAND DRUM, MARIMBA, BELLS, GLASS, TABLA, STEEL PAN, SCRAP, ZAPS, COWBELLS, SCRATCH |
| EDIT GRID |  | Draw a beat, 4 drums x 8 |
| GRID | 22 | The drawn beat plays: OFF, ON |
| GRID BEAT | 23 | A ready beat, or yours: MINE, ROCK, FUNK, HIP HOP, FOUR FLOOR, ONE DROP, HALF TIME, TRAP |
| GRID STEP | 24 | 8 steps: a bar, half, two: 1/8, 1/16, 1/4 |
| KNOCKS | 25 | Off: only the grid plays: OFF, ON |
| TUNE | 26 | The drums up or down (-12..12 st) |
| DECAY | 27 | Shorter or longer drums (0.25..4.0 x) |
| SENSE | 28 | How soft a knock counts (0..100 %) |
| SIDES | 29 | How a knock picks its drum: SIDE, TONE, ONE DRUM |
| QUANTIZE | 30 | What knocks are pulled to: OFF, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| STRENGTH | 31 | How hard they are pulled (0..100 %) |
| SWING | 32 | Late offbeats (0..75 %) |
| MUTATE | 33 | Hits drop out or change (0..100 %) |
| UNDO LAST |  | Take away the last layer |

**DRUM KIT**

| Setting | CC | |
| - | - | - |
| BODY | 35 | How loud the ringing is (0..100 %) |
| BODY PITCH | 36 | One note, or chord by side: KIT, ONE, BY SIDE |
| RING | 37 | How long it rings (0.25..4.0 x) |
| DRIVE | 38 | Dirt (0..100 %) |
| PUNCH | 39 | Squeeze: fatter, louder (0..100 %) |
| FX SEND | 40 | Into the echo and reverb (0..100 %) |

### BASS

**BASS**

| Setting | CC | |
| - | - | - |
| MODE | 46 | Knock, hum, follow or line: KNOCK, HUM, FOLLOW, LINE |
| EDIT LINE |  | Write your own bass line |
| LINE | 48 | A ready line, or yours: MINE, ROOT EIGHTHS, OCTAVE POP, WALKING, ROOT-FIFTH, FUNK, ONE DROP, ACID SLIDE |
| LINE STEP | 49 | 16 steps: a bar, or two: 1/16, 1/8 |
| PATTERN | 50 | What follow plays: ROOT, ROOT+5, OCTAVES, WALK, ARP, ACID |
| RATE | 51 | How often follow plays: 1/4, 1/8, 1/16 |
| LOCK KICK | 52 | On: play with the kicks: OFF, ON |
| FILL | 53 | Extra notes in between (0..100 %) |
| OCTAVE | 54 | The whole bass up or down (-1..1) |
| LEAN CHORD | 55 | Lean to change the chord: OFF, ON |
| RIFF | 56 | The acid riff and fills (1..16) |
| LINE 1 | 71 | The line's steps (0..16777215) |
| LINE 2 | 72 | The line's steps (0..16777215) |
| LINE 3 | 73 | The line's steps (0..16777215) |
| LINE 4 | 74 | The line's steps (0..16777215) |
| LINE 5 | 75 | The line's steps (0..16777215) |

**BASS TONE**

| Setting | CC | |
| - | - | - |
| SOUND | 57 | The bass itself: SUB, ACID, 808, FINGER, REESE, FM |
| CUTOFF | 58 | The filter: dark to bright (60..6000 Hz) |
| RESONANCE | 59 | The filter's peak (0..100 %) |
| ENVELOPE | 60 | How far each note opens it (0..100 %) |
| DECAY | 61 | How long a note rings (0.05..2.0 s) |
| GLIDE | 62 | Slide on joined notes (0..0.5 s) |
| DRIVE | 63 | Grit (0..100 %) |
| ECHO SEND | 70 | Into the shared echo (0..100 %) |

### LEAD

**LEAD**

| Setting | CC | |
| - | - | - |
| MODE | 78 | Hum it, tilt it, or it plays: HUM, TILT, ARP |
| PATTERN | 79 | How arp plays the chord: UP, DOWN, UP-DOWN, RANDOM, OCTAVES, RIFF 1, RIFF 2 |
| RATE | 80 | How fast arp plays: 1/8, 1/16, 1/8T |
| RANGE | 81 | Octaves arp climbs (1..3 oct) |
| GATE | 82 | How long each arp note is (10..100 %) |
| OCTAVE | 83 | The whole lead up or down (-2..2) |
| SNAP | 84 | Hum: pulled to the scale (0..100 %) |
| SENSE | 85 | Tilt: the softest knock (0..100 %) |

**LEAD SOUND**

| Setting | CC | |
| - | - | - |
| SOUND | 86 | The lead itself: SAW, SQUARE, SYNC, SUPERSAW, FM BELL, CHIP, WHISTLE, BRASS |
| COLOUR | 87 | Each sound's own character (0..100 %) |
| CUTOFF | 88 | The filter: dark to bright (150..9000 Hz) |
| RESONANCE | 89 | The filter's peak (0..100 %) |
| ENVELOPE | 90 | How far each note opens it (0..100 %) |
| ATTACK | 91 | A soft or sharp start (0.002..1.0 s) |
| DECAY | 92 | How fast a note settles (0.05..3.0 s) |
| GLIDE | 93 | Slide on joined notes (0..0.5 s) |
| VIBRATO | 94 | Wobble on held notes (0..100 %) |
| DRIVE | 95 | Grit (0..100 %) |
| ECHO SEND | 96 | Into the shared echo (0..100 %) |

### KEYS

**KEYS**

| Setting | CC | |
| - | - | - |
| MODE | 104 | It plays, or you knock: COMP, KNOCK |
| PATTERN | 105 | The rhythm comp plays: WHOLE, HALF, QUARTERS, OFFBEAT, STABS, ARPEGGIO, GOSPEL, BOSSA |
| VOICING | 106 | How the chord is spread: CLOSE, OPEN, SEVENTHS, SPREAD |
| OCTAVE | 107 | The hands up or down (-1..1) |
| SWING | 108 | Late offbeats (0..75 %) |
| HUMANIZE | 109 | Looser time and touch (0..100 %) |
| TOUCH | 110 | How hard comp plays (0..100 %) |
| PEDAL | 111 | On: every note rings on: OFF, ON |

**KEYS SOUND**

| Setting | CC | |
| - | - | - |
| SOUND | 112 | The instrument itself: PIANO, E.PIANO, ORGAN, PAD, CLAV, VIBES |
| TONE | 113 | Dark to bright (0..100 %) |
| RELEASE | 114 | How long a note stays off (0.05..4.0 s) |
| MOTION | 115 | Tremolo, the organ's rotor (0..100 %) |
| DRIVE | 116 | Grit (0..100 %) |
| ECHO SEND | 117 | Into the shared echo (0..100 %) |

### BREATH

**BREATH**

| Setting | CC | |
| - | - | - |
| MODEL |  | What you are blowing into: OCARINA, FLUTE, PAN PIPE, SHAKUHACHI, CLARINET, BOTTLE |
| OCTAVE |  | Shift all notes by octaves (-2..2) |
| NOTES |  | Notes on the tilt ladder (3..15) |
| GLIDE |  | Time to slide to a note (0.002..1.0 s) |
| VIBRATO |  | Pitch wobble, always on (0..100 %) |
| HARMONY |  | A second voice: OFF, 3RD UP, 5TH UP, OCTAVE UP, OCTAVE DN, DRONE |
| FX SEND |  | Into the echo and reverb (0..100 %) |
| HOLD |  | On: a drone when not live: OFF, ON |

**BLOW**

| Setting | CC | |
| - | - | - |
| SENSITIVITY |  | More: soft breath, loud (0..100 %) |
| GATE |  | More: ignore the room (0..100 %) |
| OVERBLOW AT |  | Jump up here. 100: never (50..100 %) |
| BREATHINESS |  | How much air you hear (0..100 %) |
| BRIGHTNESS |  | Dark to bright (0..100 %) |
| BEND RANGE |  | How far the stick bends (1..12 st) |
| SETUP |  | Turn: quiet 2 s, then blow: -, GO |

### HOWL

**HOWL**

| Setting | CC | |
| - | - | - |
| MODE |  | Air needs the speaker: INNER, AIR, BOTH |
| GAIN |  | Moves the howling edge (0..100 %) |
| RING |  | How narrow each note is (6..150) |
| HOWL LEVEL |  | The loudest a howl gets (-30..-6 dB) |
| FREEZE |  | Howls on under the others: OFF, ON |
| OCTAVE |  | Where the howls sit (3..6) |
| BANDS |  | How many notes can howl (3..8) |
| GLIDE |  | Slide to a new chord (0.02..4.0 s) |

**HOWL ROOM**

| Setting | CC | |
| - | - | - |
| ROOM |  | A room inside the loop (0..100 %) |
| SIZE |  | The inner room's delay (0.002..0.25 s) |
| SHIFT |  | Howls climb or fall (-20..20 Hz) |
| COLOUR |  | How the loop saturates: SOFT, WARM, HARD, FOLD |
| HEAT |  | How hard the colour bites (1..10 x) |
| DRIFT |  | Slow detune: howls beat (0..50 ct) |
| SWELL |  | How slowly howls grow (0.2..10.0 s) |
| SEND |  | Into the echo and reverb (0..100 %) |

### SONG, MIX, FX, LOOPS, LINK

**SONG**

| Setting | CC | |
| - | - | - |
| LAYER |  | The layer you play, or mix: DRUMS, BASS, LEAD, KEYS, BREATH, HOWL, MIX |
| TEMPO |  | Till the first loop sets it (60..180 bpm) |
| KEY |  | The key of every layer: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE |  | The notes every layer uses: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| CHORDS |  | How the chords move by bar: HOLD, I V vi IV, vi IV I V, I vi IV V, I IV, ii V I, i VII VI V, i iv, 12 BAR BLUES |
| HOLD CHORD |  | The chord for chords: hold: I, II, III, IV, V, VI, VII |
| CHORD BARS |  | How long each chord lasts: 1 BAR, 2 BARS, HALF BAR |
| FIRST LOOP |  | Bars, or it sets the tempo: FITS TEMPO, SETS TEMPO |

**MIX**

| Setting | CC | |
| - | - | - |
| DRUMS |  | Drums level (0..100 %) |
| BASS |  | Bass level (0..100 %) |
| LEAD |  | Lead level (0..100 %) |
| KEYS |  | Keys level (0..100 %) |
| BREATH |  | Breath level (0..100 %) |
| HOWL |  | Howl level (0..100 %) |
| MUTE DRUMS |  | Silence the drums: PLAY, MUTE |
| MUTE BASS |  | Silence the bass: PLAY, MUTE |
| MUTE LEAD |  | Silence the lead: PLAY, MUTE |
| MUTE KEYS |  | Silence the keys: PLAY, MUTE |
| MUTE BREATH |  | Silence the breath: PLAY, MUTE |
| MUTE HOWL |  | Silence the howl: PLAY, MUTE |

**FX**

| Setting | CC | |
| - | - | - |
| ECHO |  | How loud the echo is (0..100 %) |
| ECHO TIME |  | In time with the song: 1/8, 1/8 DOT, 1/4, 1/4 DOT, 1/2 |
| REPEATS |  | How long the echo goes on (0..90 %) |
| REVERB |  | How loud the reverb is (0..100 %) |
| REVERB TIME |  | How long the room rings (0.3..6.0 s) |
| REVERB TONE |  | Dark to bright (0..100 %) |

**LOOPS**

| Setting | CC | |
| - | - | - |
| CLEAR DRUMS |  | Erase the drums' loop |
| CLEAR BASS |  | Erase the bass's loop |
| CLEAR LEAD |  | Erase the lead's loop |
| CLEAR KEYS |  | Erase the keys' loop |
| CLEAR BREATH |  | Erase the breath track |
| CLEAR HOWL |  | Erase the howl track |
| CLEAR ALL |  | Every loop; the tempo free |

**LINK**

| Setting | CC | |
| - | - | - |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

## MIDI

| | |
| - | - |
| out | DRUMS on channel 10 (General MIDI drum notes), BREATH on the MIDI channel, BASS on the next, HOWL on the one after; settings as CC when changed on the box; program change when the sound changes |
| in | on the MIDI channel: notes and CCs go to the live layer; CC 20+ set settings; program change picks a sound; note 60 is REC |

## When something is wrong

- Knocks are missed or it plays by itself: DRUMS > SENSE, SYSTEM > MIC GAIN.
- Breath is not heard, or the room sets it off: BREATH > SETUP.
- BREATH or HOWL keeps sounding after you move on: BREATH > HOLD or HOWL > FREEZE is on. A loop that keeps
  playing: tap REC on that layer, or mute it in the mixer.
- HOWL in AIR needs the speaker; on headphones use INNER.
- After a restart that was not clean the screen shows CRASH, BROWNOUT or WATCHDOG for a moment; three in a
  row start it with factory settings.
