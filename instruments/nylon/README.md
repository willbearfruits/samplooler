# NYLON

For a classical guitar. Stick the board on the guitar's top: the mic hears the strings, and lifting the neck is
the pedal. Ten effects on one knob, and under all of them a looper you work with one button:

- **CLEAN**: the guitar as it is. The neck rides the loop: lower it and the loop ducks, or (TAPE LOOP) it plays the
  loop faster, slower or backwards, as on the Samplooler.
- **CLOUD**: grains from the last seconds of what you played, flying around what you play now. Push the stick
  (any way) and the cloud freezes. Play over it, then push again to let it go.
- **BLOOM**: the note you just played, held after the string dies. Lift the neck and it holds for good. Lower it
  and it stops. HARMONICS brings in the octave and the twelfth as the note ages, the way feedback does on an amp.
  With a speaker or a stick-on exciter on the top (BLOOM ON WOOD), the strings themselves keep ringing.
- **ECHO**: a tape echo. Lift the neck and the repeats run away.
- **WAH**: the neck is the pedal. FUZZ WAH has a fuzz before it.
- **SHIMMER**: a room that rises in octaves.
- **OCTAVE**: an octave under, an octave over, a fifth.
- **WOBBLE**: a warped tape's vibrato, or a chorus.
- **GOLPE**: a slap on the top is a kick, a slap on the side a snare. Loop them under your playing. NECK > SLAP
  SETUP learns the difference between your strumming and your slaps.
- **VOICE**: a bass, a bowed string, an organ or a pipe that follows the note you play. The note also goes out as
  MIDI.

Firmware for the Samplooler Stack (ESP32-S3). Built on 2026-10-24 and checked in the host simulator
(`tools/sim`, with a pretend guitar). It runs on a board and has been tried on a guitar. Those tries turned the
neck's direction round, gave NECK SETUP its four steps, added HIGH CUT and HOWL GUARD, moved the freeze to the
stick, added SLAP SETUP, kept the button's click out of the loop, and gave the looper bars and a metronome.

> PHOTO: the board on a guitar's upper bout

## Fitting it to the guitar

- Stick the board on the top with the screen facing the room. Any angle works: by the bridge standing up with the
  stick at the bottom (as it was first played), or on the upper bout away from your strumming arm. Use removable
  putty or a soft clamp; don't glue it to a French-polished top.
- One USB cable powers it, or a small power bank in a pocket. Take the sound from the line out to an amp, a PA
  or headphones.
- Turn the speaker off (SYSTEM > SPEAKER) unless you are using BLOOM ON WOOD. A speaker on the guitar plays into
  its own mic.
- A footswitch helps, because both your hands are busy. Wire a jack in parallel with the REC button (GPIO 3 to
  ground); the switch does what REC does. A sustain pedal over MIDI (CC 64) does it too.
- BLOOM ON WOOD needs the speaker amp's output on the top: the small speaker pressed against it, or better a
  stick-on exciter (a surface transducer, 4 or 8 ohm, a few watts) in its place.

## First time

1. The core asks for TILT SETUP the first time, as every instrument here does (it learns how the sensor sits on
   the board). Do it with the board lying on a table.
2. Stick it on the guitar and sit as you play. The first time it rests standing up, NECK SETUP starts by itself:
   - **1/4** sit still a second: that is home;
   - **2/4** lift the neck as far as you'd ever lift it, and hold it there: that sets which way is up and how far
     is "all the way" (NECK RANGE);
   - **3/4** back to where you sit;
   - **4/4** tip the top up towards your face and hold it: that sets the lean's direction and range (the knob
     skips this step).
   The screen shows the angle as you go, and at the end what it measured. REC stops it, and then it doesn't ask
   again; NECK > NECK SETUP runs it any time.
3. Whenever you sit differently, hold the knob: that is home again.

## Feedback

A mic a few centimetres from the strings, and a speaker or an amp nearby, can start one frequency howling.

- **HOWL GUARD** (PLAY, on to start) listens for a howl from 1 kHz up: a single high line that doesn't die away.
  When it hears one, it puts a narrow notch on that frequency within about a quarter of a second, and says HOWL
  CUT and the frequency. It can hold four notches; each is let go 30 s after its howl was last heard.
- **HIGH CUT** (PLAY, 9 kHz to start) takes the top off the mic: lower it for less fizz and whistle.
- **LOW CUT** takes out the body's boom.
- Speaker off, and the amp further away or turned from the guitar, is always the first fix.

## Playing

1. Turn the knob for a sound. Every sound is one of the ten modes with its own settings, and the loop keeps
   playing under whichever you pick.
2. Lift the neck and lower it. Each sound says at the bottom of the screen what the neck does to it, with a bar
   for how much. The small guitar on the screen lifts its neck with yours.
3. Tap REC. The metronome counts one bar in (LOOP > TEMPO, 90 to start; the header counts IN 4, 3, 2, 1), then it
   records from the "one", and after four bars (LOOP > LENGTH) it loops by itself. Nothing of the button's click
   is in it. With the metronome OFF, the take starts with your first note instead.
4. Tap REC while it plays: a new layer goes on from just after you let go. Tap again to stop adding. LOOP > UNDO
   takes the newest layer off.
5. Hold REC to stop the loop; tap to start it again from the top. Hold REC on a stopped loop, or keep holding for
   2 s while it plays, to erase it.

### The metronome and bars

- **LENGTH** (LOOP page): 1, 2, 4 or 8 bars, or FREE. In bars, the loop closes by itself, to the sample. In FREE,
  you tap REC where it should close; with the metronome on, it closes on the nearest bar line.
- **TEMPO** and **BEATS** (a bar) set the grid. Set them to your piece's.
- **METRONOME**: COUNT-IN (to start: it ticks one bar in, then stops while you record, so a speaker the mic hears
  puts no tick in the loop), SOUND (it ticks through the take as well), LIGHT (the LED flashes white on the beat)
  or OFF. The tick is never recorded into the loop by the looper, but it does come out of the line out and the
  speaker.
- **Without the metronome**, **START** decides: ON A NOTE (the take starts with your first note, 0.8 s or more
  after the tap) or AT ONCE (25 ms after you let go of REC; a loop tapped on two downbeats then comes out about
  0.1 s short, so use ON A NOTE or the metronome for a loop in time). A LENGTH in bars then counts from your first
  note at TEMPO.

## Controls

| | |
| - | - |
| lift / lower the neck | what the sound says at the bottom of the screen (more / less, or one way / the other) |
| tip the top up towards you | a second gesture in some sounds (BLOOM's harmonics, ECHO's time, OCTAVE's fifth) |
| tap REC | record (a bar of count-in, or your first note), add a layer, stop adding, play again; in LENGTH FREE, close the loop |
| hold REC | stop the loop; on a stopped loop, erase it; held 2 s while it plays, erase it |
| push the stick, any way | CLOUD: freeze, let go |
| slap the top / a side | GOLPE: a drum |
| turn the knob | next or previous sound |
| hold the knob | home: where the neck is now reads zero |
| tap the knob during NECK SETUP's last step | skip the tip |
| press the knob | the menu |
| press the knob twice | the knob turns the volume until it rests 3 s |

## Sounds

CLEAN, TAPE LOOP, CLOUD, HALO (long grains, the neck all the way up freezes it), DUST (tiny grains), BLOOM,
BLOOM ON WOOD, HARMONICS, ECHO, DUB (the echo fitted to the loop), SLAPBACK, NECK WAH, FUZZ WAH, AUTO WAH,
SHIMMER, CATHEDRAL, OCTAVES, TWELVE STRING, WOBBLE, CHORUS, GOLPE, CAJON, BASS, BOW, ORGAN. There are eight more
slots for your own, under SOUND > SAVE AS.

## MIDI (USB)

| Out | |
| - | - |
| notes | VOICE: the note you play (shifted by its OCTAVE), as long as it sounds |
| notes on channel 10 | GOLPE: every slap's drum, with its velocity |
| CC 1, CC 2 | the neck and the lean, 0 to 127 (64 at home), with SYSTEM > MIDI CC OUT on |
| CC 20 and up | every setting, when you change it |

| In | |
| - | - |
| CC 64 (sustain pedal), note 60 | REC |
| note 61, 62, 63 | UNDO, stop, erase (for a MIDI footswitch) |
| CC 20 and up, program change | settings, sounds |

## Building

ESP-IDF v5.5.1, as for the Samplooler (the core's files are copied in from `core/`, 2026-10-24):

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name over USB and flashes only that one
../tools/sim/run.sh        # the simulator: every check, screens, GIFs and sound in tools/sim/out/
```

The full manual is [docs/MANUAL.md](docs/MANUAL.md).
