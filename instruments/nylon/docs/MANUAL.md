# NYLON manual

## The neck

NYLON reads the guitar's angle from the board's motion sensor. When you lift the neck, the guitar turns about its
top's normal (the line straight out of the top). NYLON measures that turn from **home**, where you sit when you
play.

- **Home** is taken by itself the first time the board rests for a second and a half. It is taken again if the
  board goes from lying flat to standing up, or back (from the table to the guitar). Hold the knob, or NECK >
  SET HOME, to make wherever you are home. A home you set is kept after a power cycle; one taken by itself is not.
- **NECK SETUP** learns your guitar. It starts by itself the first time the board rests standing up without one
  done; REC stops it (then it doesn't ask again) and NECK > NECK SETUP runs it any time. Four steps, each waiting
  for the box to be still at its furthest point:
  1. sit as you play and hold still for a second: home;
  2. lift the neck as far as you'd lift it, and hold it: the axis, which way is up, and NECK RANGE (0.85 of how
     far you went, so the top of the range is easy to reach);
  3. back to where you sit;
  4. tip the top up towards your face and hold it: which way the lean goes (LEAN) and LEAN RANGE. The knob skips
     this step; after 15 s it is skipped by itself.

  The screen counts the degrees as you go and shows at the end what it measured. After it the board can sit at any
  angle on the top, and the guitar can be left-handed.
- **Without NECK SETUP**, a board standing up takes its own screen's normal as the axis. That is right for a
  board stuck on the top with the screen facing the room and a right-handed player. (The first version had
  this the wrong way round: lifting read as lowering. Fixed on 2026-10-24.)
- **Lift** is that angle in degrees: neck up is positive, neck down negative. **Lean** is the turn across it:
  the top tipped up towards your face is positive (NECK > LEAN can flip it or turn it off).
- **STILL ZONE** degrees either side of home do nothing, so playing and breathing don't move the sound.
  **NECK RANGE** degrees is the whole way for the lift, **LEAN RANGE** for the tip. In between, the sound moves smoothly. With the defaults, 3 degrees do
  nothing and 20 degrees is everything.
- A slap jolts the accelerometer but does not move the neck reading. The sensor is low-passed over about a tenth
  of a second, and short jolts are left out.

The small guitar on the left of the screen lifts its neck with yours. The dotted arc around it is NECK RANGE, and
its tick is home. At the bottom of the screen, NECK: says what the neck does in this sound, with a bar for how
much (one centred on home where it goes both ways).

## The looper

One button: REC, the BOOT button, a footswitch wired in parallel with REC (GPIO 3 to ground), a MIDI sustain
pedal (CC 64) or MIDI note 60.

| The loop is | tap | hold (0.6 s) |
| - | - | - |
| empty | the metronome counts a bar in and it records from the next "one"; without it, armed: the take starts on your first note (START AT ONCE: as you let go) | |
| armed | not armed any more | |
| recording | LENGTH FREE: the loop closes (on the nearest bar line with the metronome); in bars it closes by itself | the take is dropped |
| playing | a layer goes on from just after you let go | stopped; held on to 2 s, erased |
| adding | the layer ends just before the tap | stopped; held on to 2 s, erased |
| stopped | it plays from the start | erased |

**The metronome and bars.** LENGTH is 1, 2, 4 (to start) or 8 bars of TEMPO (90 to start) and BEATS (4), or FREE.
With the METRONOME on, a tap on REC starts the grid 0.8 s later, counts one bar in (the header says IN 4, 3, 2, 1)
and the take starts exactly on the next bar line; in bars it closes by itself after exactly that many bars (halved
until it fits the buffer); in FREE a tap closes it on the nearest bar line (at least one bar). COUNT-IN (to start)
ticks only during the count-in; SOUND ticks through the take too; LIGHT flashes the LED white on each beat (the
bar's first brighter); the ticks are 2.1 kHz on the bar's first beat, 1.5 kHz on the others. The tick is added
after the looper, so the looper never records it; a speaker the mic hears would, so COUNT-IN keeps the take clear
of it. Without the metronome, START decides: ON A NOTE (the take starts at your first note) or AT ONCE (as you let
go), and a LENGTH in bars counts from there at TEMPO.

**The button's click.** The REC button is on the guitar's top, and the mic hears it click going down and coming
up. None of that goes into the loop:
- **arming:** for 0.8 s after the tap nothing can start the take;
- **a take** starts after the metronome's bar of count-in, or at a note heard after the 0.8 s (20 ms before it, so
  its attack is in), or (AT ONCE without the metronome) 25 ms after you let go. That last way makes a loop tapped
  exactly on two downbeats about 0.1 s short, so use ON A NOTE or the metronome for a loop in time;
- **closing** in FREE: the loop is [first note - 20 ms, tap - 20 ms), so your first note sounds again exactly a
  loop after you played it. Only 6 ms of what came after the loop's end, from before the tap's click, is
  crossfaded into its start, so the seam is smooth;
- **a layer** starts 25 ms after you let go of the tap that starts it, and ends 15 ms before the tap that ends it.
  It is written 40 ms behind the play head, from what was played 40 ms before, which is how its end can sit
  before a click that was already heard;
- **cancelling:** tapping an armed take away can itself start a take through its click; a take that began less
  than 0.1 s before the tap is dropped with it.

A note louder than LOOP > TRIGGER counts as a note. The board keeps the last second of what came out at all times,
so a take's run-up and a layer's start are there when they are needed.

- **The loop records everything NYLON plays:** the dry guitar, the effect, the drums and the room. Changing the
  sound changes what you play next, not what is in the loop.
- **UNDO** (LOOP > UNDO, or MIDI note 61) takes the newest layer off with a 30 ms crossfade. It works while the
  loop fits twice in the buffer: up to 11 s of loop with the 22 s buffer (four bars down to 88 BPM in 4/4). A
  longer loop can't keep a copy, and UNDO says so.
- **KEEP** below 100 % turns the loop into a fading echo: each pass keeps that much of what is there.
- **LOOP LEVEL** sets how loud the loop plays under you.
- **BY THE NECK** sets what the neck does to the loop, in any sound. LEVEL: lower the neck to duck the loop
  (gone at the bottom of the range), lift it to bring the loop up to 1.4 times. TAPE: lifting plays it up to
  twice as fast (an octave up); lowering slows it, it stands still half way down and plays backwards at full
  speed at the bottom. CLEAN rides the level, TAPE LOOP the speed. While a layer is being added, the tape
  glides back to 1x.

The buffer is 22 s of PSRAM (18, 14, 10 or 8 if the board has less free). A take that reaches it closes by itself. Takes
shorter than a quarter of a second are taken as a slip and dropped.

The loop goes round the soundhole on the screen. While recording, an arc grows with a blinking head; while
playing, a ring with the head going round and a tick at the loop's start; while adding, a double ring. A dot
above the ring stands for each layer, and in bars a tick outside it marks each bar. The header says ARMED (with the
metronome, IN and the beats of count-in left), REC and the bar (REC 2/4) or the seconds
so far, LOOP and the layers, ADDING, STOPPED, or the tape's speed (TAPE 1.5X, BACK 1.0X).

## The modes

Each sound is one MODE with its settings, and the knob walks the sounds. DRY is the guitar as it is, EFFECT how
loud the mode's own sound is, ROOM how much goes into the room (the room has a SHIMMER, which SHIMMER uses).
When you turn to a sound with another mode, the new effect fades in over 10 ms. The old one stops getting your
guitar but rings out (an echo's repeats, the cloud's last grains) for up to 6 s. A held BLOOM note is let go over
0.3 s.

### CLEAN

The guitar, a little room, and the neck on the loop (LEVEL; TAPE in TAPE LOOP).

### CLOUD

The last three seconds of the guitar are always kept. CLOUD plays grains from anywhere in REACH of them: GRAIN
SIZE long, DENSITY a second. SPARKLE of them an octave up, DEPTH an octave down, and REVERSE of them
backwards. The cloud's grains are about as loud together whatever the density.

- **The neck:** lift it for more grains (up to 2.5 times), longer (up to 1.6 times) and louder; lower it for a
  thin, quiet cloud.
- **The lean:** tipped up, more grains an octave up; tipped down, more an octave down.
- **FREEZE BY THE STICK** (to start): push the stick any way and the writing stops. The cloud is then what was
  there, and you play over it. Push again to let it go. Strumming can't do it. (A board without a stick uses NECK
  UP.)
- **FREEZE BY A SLAP:** a slap on the body does the same (see SLAP SETUP).
- **FREEZE BY NECK UP:** the neck past 85 % of its range freezes it, and below 70 % lets it go (HALO).

The screen shows grains flying out of the soundhole, further for older sound, as sparks for those an octave up.
When frozen, they circle and a snowflake sits in the hole.

### BLOOM

Each note you play (CATCH) is caught 40 ms after its attack for 0.3 s, into one of three short buffers in turn.
It is played back by overlapping grains from random places in it. The result is the same note or chord, still,
without its attack.

- **SWELL**: how long it takes to grow in while the string dies.
- **HOLD**: how long it then stays. At 100 % it stays for good; lower, it fades over a quarter of a second up to
  16 s.
- **HARMONIC**: the octave and the twelfth grow in as it ages (from half a second to 3 s), as feedback does on an
  amp. **SUB**: an octave under it.
- **CATCH**: EVERY NOTE takes each new note. LOUD NOTES takes only those near the last loud one. THE FIRST holds
  the first note until it is stopped.
- **The neck:** lift it for a longer hold (about 70 % of the range up holds for good) and louder. Lowered past 60
  % of the range, it stops the note, which is then let go.
- **The lean:** up brings the harmonics in, down the octave under.
- **A slap on the body** does not stop it, and the slap's thump is not taken as a note to hold.

The random grains make a held note shimmer a little, like a chorus. They are not a perfect copy.

BLOOM ON WOOD has no dry guitar and no room. Put the speaker, or better a stick-on exciter (a surface transducer
on the MAX98357A's output), on the guitar's top. The held note then drives the top, and the top rings the
strings that are in tune with it. The guitar keeps sounding by itself. This is the experiment in NYLON: the
simulator shows the loop working with a pretend guitar, and nobody knows yet how strongly a real classical top
answers 3 W from a USB supply. Because BLOOM only replays what it caught and never feeds the mic straight back,
it does not howl.

### ECHO

A tape echo, 1.5 s long, in PSRAM.

- **ECHO TIME** glides when it changes, so a change bends the repeats' pitch, as a tape's speed would.
- **REPEATS**: how much comes back each time. **ECHO TONE**: how dark the repeats get. **TAPE WOW**: how much the
  tape wavers.
- **ECHO SYNC: LOOP**: with a loop playing, the time becomes the loop's length divided by 1, 2, 3, 4, 6, 8, 12,
  16, 24 or 32, whichever is nearest ECHO TIME (DUB).
- **The neck:** lift it for more repeats. Past 100 % they run away, and the echo's own saturation holds them
  under full scale. Lower it and they die sooner.
- **The lean:** the time, up to 1.4 times shorter or longer, with the tape's pitch bend.

### WAH

A resonant band pass from 330 Hz to 2.5 kHz. With the neck at home, the pedal is half way. Lift the neck to
open it, lower it to close it.

- **WAH PEAK**: how sharp it is (the lean sharpens or softens it further).
- **AUTO WAH**: playing harder opens it.
- **FUZZ**: a fuzz before it.

The soundhole on the screen is a mouth that opens and says OOH, WAH, AAH.

### SHIMMER

The room, with its own sound shifted up an octave (SHIMMER UP: or a fifth, or both) and fed back into it, so the
tail climbs. Lift the neck for more shimmer and more room. Tip the top down for fifths.

### OCTAVE

A delay line shifter, the classic two-tap kind: OCT DOWN, OCT UP and FIFTH levels. Lift the neck to bring the
octave over up, lower it to bring the octave under up. The lean brings the fifth in. Like every shifter of this
kind it is a little rough on chords and slow notes, and the octave under is low-passed.

### WOBBLE

A delay line swung by a slow wave, a little flutter and a slow wander, like a worn tape. WOBBLE (dry 0) is a
vibrato; CHORUS (dry 100) is a chorus. Lift the neck for more depth (up to twice). The lean sets the speed.

### GOLPE

Slaps on the guitar's body are drums. The accelerometer sees the jolt and which way it went. A push along the
top's normal is the TOP (a slap on the top or a knock on the back); a push across it is a SIDE. The mic has to
hear something within 40 ms, so moving the guitar is not a drum. A jolt over 2.2 times the threshold counts
without the mic. A slap is at most one every 90 ms. During SLAP SETUP nothing is played by a slap.

- **TOP**, **SIDE**: the drums (KICK, SNARE, HAT, OPEN HAT, TOM, CLAP, RIM, COWBELL, SHAKER, CONGA, CAJON LOW,
  CAJON HIGH, OFF), KNOCK's synthesized ones and a cajon.
- **DRUM LEVEL**, **DRUM TUNE**, **DRUM DECAY**.
- **The neck:** tunes the drums up to a fifth up or down. The lean makes them ring longer or shorter.
- **NECK > SLAP SETUP** learns your guitar: strum as hard as you play for four seconds (it takes the biggest
  jolt that makes), then slap the top three times (the smallest of those). A slap is then anything over the
  geometric mean of the two. If the slaps were not clearly harder than the strumming (1.5 times), it says TOO
  ALIKE: SLAP HARDER. Until it is done, a slap is 1.2 g.
- **NECK > SLAP SENSE** scales that: 50 % is the learned value, 0 % twice it, 100 % half of it. If strumming
  still plays drums, turn it down; if slaps get lost, up.
- **NECK > SLAPS**: AUTO (drums in GOLPE, nothing elsewhere), DRUMS (drums in every sound), OFF.

The drums are recorded into the loop like everything else, so a golpe groove can be looped under the playing.

### VOICE

The pitch tracker listens to the last 48 ms of the guitar 50 times a second (McLeod's NSDF, 69 Hz to 1 kHz) and a
synth follows the note while the string sounds: BASS, BOW, ORGAN or PIPE, OCTAVE octaves under or over, GLIDE
to slide between notes.

- **The neck:** lift it to make the voice louder (up to 1.5 times), lower it to fade it out.
- **MIDI NOTES**: the note (with the OCTAVE) goes out over USB MIDI, so NYLON plays a computer's synth too.

It follows one note: play single lines. On a chord it takes the strongest pitch it hears, and that may jump.

## Feedback: HIGH CUT and HOWL GUARD

The mic sits a few centimetres from the strings, and from the board's own speaker; an amp may be in the room.
One frequency can start to feed back and climb into a whistle.

**HOWL GUARD** (PLAY > HOWL GUARD, on to start) looks at the mic's spectrum 50 times a second (after LOW CUT, HIGH
CUT and the notches it has already set). A howl is a single narrow line from 1 kHz to 14 kHz that is:
- 22 dB over the spectrum around it and 12 dB over any other peak there (a note's harmonics are a row of peaks);
- louder than -42 dBFS;
- in the same place for a quarter of a second without dying away. A plucked note's high partials fall several dB
  in that time; a howl holds at the limiter or climbs.

It gets a notch (Q 10, faded in over 50 ms) at its frequency, and the screen says HOWL CUT and the frequency.
Four notches at most; a fifth replaces the oldest. A notch is let go 30 s after its howl was last heard, and set
again if the howl comes back. Turning HOWL GUARD off clears them. In the simulator, a speaker fed back into the
mic at 4.3 kHz is notched 0.25 s after it starts and stays 68 dB down; none of the playing in the other checks
(plucks, strums, slaps, every mode, a steady sawtooth through the wah) sets a notch.

**HIGH CUT** (9 kHz to start, 1 to 14 kHz) is a low pass on the mic. It works on everything after it, and lowers
any howl above it. The SHIMMER's loop has its own low pass at about 6 kHz, so it climbs in octaves without
whistling.

## The menu

PLAY (MODE, EFFECT, DRY, ROOM, ROOM SIZE, LOW CUT, HIGH CUT, HOWL GUARD), LOOP, NECK, CLOUD, BLOOM, FX (ECHO, WAH, SHIMMER, OCTAVE and
WOBBLE settings), DRUMS, VOICE, then the core's SOUND and SYSTEM pages. LOW CUT takes out the body's boom before
anything else (90 Hz to start; up to 400 Hz for a boomy spot). The LOOP and NECK settings belong to the board,
not to a sound, and stay when you change sounds.

<!-- tables -->
### PLAY

| Setting | CC | |
| - | - | - |
| MODE | 20 | The effect on the guitar: CLEAN, CLOUD, BLOOM, ECHO, WAH, SHIMMER, OCTAVE, WOBBLE, GOLPE, VOICE |
| DRY | 21 | The guitar as it is (0..100 %) |
| EFFECT | 22 | How loud the effect is (0..150 %) |
| ROOM | 23 | A room around it all (0..100 %) |
| ROOM SIZE | 24 | A small room to a hall (0..100 %) |
| LOW CUT | 25 | Less boom from the body (40..400 Hz) |
| HIGH CUT | 26 | Less fizz and whistle (1000..14000 Hz) |
| HOWL GUARD |  | A howl gets a notch: OFF, ON |

### LOOP

| Setting | CC | |
| - | - | - |
| BY THE NECK | 70 | Lift: louder or faster: OFF, LEVEL, TAPE |
| LOOP LEVEL | 71 | How loud the loop plays (0..100 %) |
| KEEP | 72 | Each pass keeps this much (50..100 %) |
| LENGTH |  | Bars a take: it stops itself: FREE, 1 BAR, 2 BARS, 4 BARS, 8 BARS |
| TEMPO | 74 | Beats a minute (40..220) |
| BEATS |  | Beats a bar (2..7) |
| METRONOME |  | A bar in, then it records: OFF, LIGHT, COUNT-IN, SOUND |
| START |  | No metronome: when it begins: ON A NOTE, AT ONCE |
| TRIGGER |  | How loud a note starts it (-60..-12 dB) |
| UNDO |  | The newest layer goes |
| ERASE |  | The loop goes |

### NECK

| Setting | CC | |
| - | - | - |
| NECK SETUP |  | Sit still then lift |
| SET HOME |  | Where you sit is zero |
| NECK RANGE |  | Degrees for the most (8..45) |
| LEAN RANGE |  | Degrees of tip for all (6..45) |
| STILL ZONE |  | Degrees that do nothing (0..10) |
| USE NECK |  | The neck moves the sound: OFF, ON |
| LEAN |  | Tipping the top up: OFF, TOP TO ME, TOP AWAY |
| SLAPS |  | What a hit on the body does: AUTO, DRUMS, OFF |
| SLAP SENSE |  | How light a slap counts (0..100 %) |
| SLAP SETUP |  | Strum hard, then slap three |

### CLOUD

| Setting | CC | |
| - | - | - |
| GRAIN SIZE | 28 | How long each grain is (15..500 ms) |
| DENSITY | 29 | Grains a second (1..80) |
| REACH | 30 | How far back they reach (0.1..2.8 s) |
| SPARKLE | 31 | Grains an octave up (0..100 %) |
| DEPTH | 32 | Grains an octave down (0..100 %) |
| REVERSE | 33 | Grains played backwards (0..100 %) |
| FREEZE BY | 34 | What holds the cloud still: THE STICK, A SLAP, NECK UP, OFF |

### BLOOM

| Setting | CC | |
| - | - | - |
| HOLD | 35 | How long a note stays (0..100 %) |
| SWELL | 36 | How slowly it grows in (0.05..6.0 s) |
| HARMONIC | 37 | Its harmonics grow in (0..100 %) |
| SUB | 38 | An octave under it (0..100 %) |
| CATCH | 39 | Which notes it holds: EVERY NOTE, LOUD NOTES, THE FIRST |

### FX

| Setting | CC | |
| - | - | - |
| ECHO TIME | 40 | Time between repeats (0.04..1.4 s) |
| REPEATS | 41 | How long the echo goes on (0..100 %) |
| ECHO TONE | 42 | Dark or bright repeats (0..100 %) |
| TAPE WOW | 43 | The tape wavers (0..100 %) |
| ECHO SYNC | 44 | The time fits the loop: OFF, LOOP |
| WAH PEAK | 45 | How sharp the wah is (1..12) |
| AUTO WAH | 46 | Playing hard opens it (0..100 %) |
| FUZZ | 47 | Dirt before the wah (0..100 %) |
| SHIMMER | 48 | The room rises in octaves (0..100 %) |
| SHIMMER UP | 49 | Octave or fifth or both: OCTAVE, FIFTH, BOTH |
| OCT DOWN | 50 | An octave under (0..100 %) |
| OCT UP | 51 | An octave over (0..100 %) |
| FIFTH | 52 | A fifth over (0..100 %) |
| WOBBLE | 53 | How far the pitch wavers (0..100 %) |
| WOBBLE RATE | 54 | How fast it wavers (0.1..8.0 Hz) |

### DRUMS

| Setting | CC | |
| - | - | - |
| TOP | 55 | A slap on the top plays |
| SIDE | 56 | A slap on the side plays |
| DRUM LEVEL | 57 | How loud the drums are (0..100 %) |
| DRUM TUNE | 58 | The drums up or down (-12..12 st) |
| DRUM DECAY | 59 | Short or ringing drums (0.25..4.0 x) |

### VOICE

| Setting | CC | |
| - | - | - |
| VOICE | 60 | What plays along with you: BASS, BOW, ORGAN, PIPE |
| OCTAVE | 61 | Under or over your note (-2..1) |
| GLIDE | 62 | How it slides to a note (0..0.5 s) |
| MIDI NOTES |  | Your notes go out on midi: OFF, ON |
<!-- /tables -->

## MIDI

Out: CC 20 and up for every setting you change; CC 1 (the neck) and CC 2 (the lean), 0 to 127 with 64 at home,
when SYSTEM > MIDI CC OUT is on; VOICE's notes on the MIDI channel; GOLPE's drums on channel 10 (General MIDI:
36 kick, 38 snare, 35 cajon low, 40 cajon high, ...); program change on a sound change.

In: CC 64 (a sustain pedal) and note 60 are REC; notes 61, 62 and 63 are UNDO, stop and erase; CC 20 and up set
the settings; program change picks a sound.

## Tips

- Set LOW CUT first, with the guitar in your lap: up until the boom goes, no further.
- TRIGGER: if the take starts on noise, raise it; if it misses a soft first note, lower it.
- Play a loop in CLEAN, then turn to CLOUD and play over it. Or loop a golpe groove in GOLPE, add a bass line in
  BASS, then solo through NECK WAH.
- Bloom a chord, freeze it in CLOUD with a slap, and play the melody over it.
- In ECHO, lift the neck at the end of a phrase and let it run away; lower it to bring it back.

## When something is wrong

| | |
| - | - |
| The neck does nothing | NECK > USE NECK is OFF, or the tilt sensor is missing (the screen says NO NECK). Do the core's TILT SETUP. |
| Lifting reads as lowering | NECK > NECK SETUP (a left-handed guitar, or the board not facing out of the top). |
| Lifting does too little or too much | NECK > NECK SETUP again, lifting as far as you really play; or NECK RANGE by hand. |
| The sound moves while I just play | NECK > STILL ZONE up a degree or two, or NECK RANGE wider. |
| Strumming plays drums | NECK > SLAP SETUP; then SLAP SENSE down. |
| Slaps get lost | NECK > SLAP SETUP; then SLAP SENSE up; slap nearer the board. |
| The loop starts with a click | It should not any more (0.1.2). If it does, tell us: LOOP > TRIGGER up meanwhile. |
| The loop drifts against your playing | METRONOME on (COUNT-IN), LENGTH in bars, TEMPO at your piece's: the take starts and ends on bar lines. |
| The take starts on its own | LOOP > TRIGGER up (the room or the speaker is loud), or SYSTEM > MIC GAIN down. The metronome's SOUND through the speaker can start it: use LIGHT. |
| It howls | HOWL GUARD on (PLAY). SYSTEM > SPEAKER OFF; use the line out, the amp further away. HIGH CUT down, LOW CUT up. |
| A high whistle or fizz | PLAY > HIGH CUT down. |
| UNDO says it is too long | UNDO keeps a copy only for loops up to half the buffer (8 s). |
| VOICE jumps between notes | play single notes; a chord has no one pitch. |
