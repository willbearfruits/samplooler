# SING: the manual

Every page of the menu, what it does, and how SING plays with a BAND. The settings tables at the end are written
from the code (`python3 tools/manual_tables.py`).

## What happens to your voice

```
mic -> INPUT (gate, even out, de-ess, howl guard)
    -> the plain voice ............................................ PLAIN
    -> the voices: your pitch is tracked, then rebuilt grain by grain
         the tuned voice (TUNE, PITCH, THROAT) ........................ TUNED
         up to three harmonies (HARMONY) and a choir (CHOIR)
    -> the vocoder: your voice shapes a synth (VOCODER)
  = the voice -> the loop records it -> the voice (MONITOR) + the loop (LOOP LEVEL)
    -> the stutter (roll right) -> the echo and the room (ECHO + VERB) -> out
```

## VOICE

TUNE pulls your pitch to the nearest note it may land on (SONG > TUNE TO), TUNE SPEED says how fast: 0 is the
hard step of the robot, 100 ms and more a natural pull. KEEP VIBRATO lets your own wobble and slides through.
PITCH moves the tuned voice (and every harmony) up or down; THROAT moves the formants alone, so you sound bigger
or smaller on the same note. TUNED and PLAIN are the two voices' levels: a tuned sound sets PLAIN to 0, because
the two together sound doubled (the tuned one is 16 ms later).

## HARMONY

Up to three singers with you, each a fixed distance from the tuned voice:

- IN KEY: the interval in steps of the scale (a third above A in C major is C).
- CHORD: the chord's tones nearest you, above or below (3RD UP is the next chord tone up, 5TH UP the one after).
- FIXED: the same interval in semitones whatever the key (3RD UP is always 4 semitones).

The harmonies stop on s, t and breaths and come back on the next vowel, as harmony singers do.

## CHOIR

Copies of you in what is left of eight voices: UNISON (all on your note), OCTAVES (an octave above and below),
CHORD (the chord's tones around you). SPREAD puts them out of tune with each other, LOOSENESS makes them come in
late, HUMAN lets each one drift on its own.

## VOCODER

Your voice shapes a synth: its bands follow how loud your voice is in each. PLAYS: the song's chord (or the notes
a MIDI keyboard holds), the note you sing tuned (a robot that sings in tune), both, or noise (a whisper). BANDS:
more is clearer, fewer is more synth. ESSES puts your s and t on top so the words stay clear.

## ECHO + VERB

The echo is in time with the song (ECHO TIME in beats); DUCK keeps it down while you sing and lets it swell in the
gaps. The room: REVERB, ROOM SIZE (seconds to die away), ROOM TONE, and SHIMMER, an octave up ringing in it.
Rolling the box left throws your voice into the echo and lengthens its repeats while you hold it there.

## SONG

The key and the chords the tuning, the harmonies and the vocoder use. TUNE TO: SCALE, CHORD (only the chord's
notes: strong), ALL NOTES, PENTATONIC, BLUES. TEMPO, KEY, SCALE, CHORDS, HOLD CHORD and CHORD BARS are shared with
a linked BAND, both ways, and stay when you change the voice. The chord moves by bar within the session's loop,
as the BAND counts it, so a loop always meets the same chords.

## INPUT

GATE silences the room between words, EVEN OUT brings soft and loud singing closer, DE-ESS softens sharp s
sounds. HOWL GUARD watches for a tone that rings and keeps growing while the speaker is loud (feedback; a sung
note moves, feedback does not) and notches it, up to three at a time, turning the input down if it still grows.
MONITOR: how loud your live voice comes out; lower it when the board's own speaker is close to the mic.

## LOOP

- LOOP: BARS makes every loop whole bars on the session's beat (or a multiple of the session's loop); FREE is the
  Samplooler's own, as long as you hold REC, not in time with anything.
- Alone, with nothing in time yet, the first loop (BARS) sets the tempo: 1, 2, 4 or 8 bars, whichever puts the
  tempo nearest 110 BPM.
- REC let go early records on to the end of the bar; hold too long and the extra goes.
- LOOP START: SOUND (the default) starts the take at your first sound after the press, so the silence before it is
  cut; PRESS starts it at the press. Either way REC's own clicks stay out: nothing of the first 0.12 s after a press
  is used, and the moment of letting go is cut out (or silenced, while the take records on to the end of the bar).
  Holding REC over silence and letting go makes no loop.
- LOOP KEEP below 100 % fades the older layers each time you add.
- To redo the loop: tap REC to stop it, then hold REC. The new take replaces the old one. CLEAR LOOP erases it.
- TILT: LOOP SPEED (the Samplooler), PITCH, FORMANT or OFF. ROLL: ECHO+STUTTER or OFF.
- SPEED MODE, for TILT: LOOP SPEED. KEEP PITCH (the default): the tilt changes how fast the loop goes, and
  backwards, but every note stays at the pitch you sang it, so the loop stays in key with a BAND. It is cut into
  64 ms grains to do that, which you can hear as a slight blur; held at a stop it smears the moment it stopped on.
  OCTAVES: tape speed, but only 2x, 1x, half speed, half speed backwards and backwards, with a short slide between
  them, so it moves by octaves and stays in key. TAPE: the Samplooler's own, the pitch goes with the speed.

## LINK

LINK on: SING looks for other boards in the same GROUP every 100 ms. WHO IS THERE lists them. RANGE: NEAR (2
dBm), ROOM (8), FAR (14); every packet is a burst of current from the same supply as the DAC, so if you hear a
faint ticking at ten a second, try NEAR.

## MIDI

Notes in are the chord while they are held. The sustain pedal is REC. Out: the tuned note on the MIDI channel,
note on and off as you sing. Every setting below is on its CC both ways; program change picks a voice.

## Settings

<!-- tables -->
**VOICE**

| Setting | CC | |
| - | - | - |
| TUNE | 20 | How hard it pulls to a note (0..100 %) |
| TUNE SPEED | 21 | 0: the hard robot step (0..400 ms) |
| KEEP VIBRATO | 22 | Your own wobble stays (0..100 %) |
| PITCH | 23 | The voice up or down (-24..24 st) |
| THROAT | 24 | Bigger or smaller singer (-12..12 st) |
| TUNED | 25 | The tuned voice's level (0..100 %) |
| PLAIN | 26 | Your voice as it is (0..100 %) |

**HARMONY**

| Setting | CC | |
| - | - | - |
| HARMONY | 27 | How the voices find notes: OFF, IN KEY, CHORD, FIXED |
| VOICE 1 | 28 | A singer above or below: OFF, 3RD UP, 4TH UP, 5TH UP, 6TH UP, OCT UP, 3RD DOWN, 4TH DOWN, 5TH DOWN, 6TH DOWN, OCT DOWN |
| VOICE 2 | 29 | A second one: OFF, 3RD UP, 4TH UP, 5TH UP, 6TH UP, OCT UP, 3RD DOWN, 4TH DOWN, 5TH DOWN, 6TH DOWN, OCT DOWN |
| VOICE 3 | 30 | A third one: OFF, 3RD UP, 4TH UP, 5TH UP, 6TH UP, OCT UP, 3RD DOWN, 4TH DOWN, 5TH DOWN, 6TH DOWN, OCT DOWN |
| HARM LEVEL | 31 | How loud they sing (0..100 %) |

**CHOIR**

| Setting | CC | |
| - | - | - |
| CHOIR | 32 | How many of you, how loud (0..100 %) |
| CHOIR TYPE | 33 | Same note, octaves, chord: UNISON, OCTAVES, CHORD |
| SPREAD | 34 | How out of tune they are (0..40 c) |
| LOOSENESS | 35 | How late they come in (0..60 ms) |
| HUMAN | 36 | Slow drift in every voice (0..100 %) |

**VOCODER**

| Setting | CC | |
| - | - | - |
| VOCODER | 37 | Your voice plays a synth (0..100 %) |
| PLAYS | 38 | The chord, your note, noise: CHORD, NOTE, CHORD+NOTE, NOISE |
| OCTAVE | 39 | The synth up or down (-2..2) |
| BANDS | 40 | More: clearer words (8..20) |
| SHARPNESS | 41 | Smooth or ringing bands (0..100 %) |
| RELEASE | 42 | How long the bands ring (5..300 ms) |
| ESSES | 43 | Your s and t on top (0..100 %) |
| BUZZ | 44 | Dark or buzzy synth (0..100 %) |
| DETUNE | 45 | The synth's width (0..30 c) |

**ECHO + VERB**

| Setting | CC | |
| - | - | - |
| ECHO | 46 | How loud the echo is (0..100 %) |
| ECHO TIME | 47 | In time with the song: 1/16, 1/8, 1/8 DOT, 1/4, 1/4 DOT, 1/2, 1 BAR |
| REPEATS | 48 | How long it goes on (0..95 %) |
| ECHO TONE | 49 | Dark tape to bright (0..100 %) |
| DUCK | 50 | Echo waits while you sing (0..100 %) |
| REVERB | 51 | How loud the room is (0..100 %) |
| ROOM SIZE | 52 | How long it rings (0.3..12.0 s) |
| ROOM TONE | 53 | Dark to bright (0..100 %) |
| SHIMMER | 54 | An octave up in the room (0..100 %) |

**SONG**

| Setting | CC | |
| - | - | - |
| TUNE TO | 55 | The notes it tunes to: SCALE, CHORD, ALL NOTES, PENTATONIC, BLUES |
| TEMPO | 56 | Shared with a linked band (60..180 bpm). Kept when the voice changes |
| KEY | 57 | The key you sing in: C, C#, D, D#, E, F, F#, G, G#, A, A#, B. Kept when the voice changes |
| SCALE | 58 | The notes of the key: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR. Kept when the voice changes |
| CHORDS | 59 | How the chords move by bar: HOLD, I V vi IV, vi IV I V, I vi IV V, I IV, ii V I, i VII VI V, i iv, 12 BAR BLUES. Kept when the voice changes |
| HOLD CHORD | 60 | The chord for chords: hold: I, II, III, IV, V, VI, VII. Kept when the voice changes |
| CHORD BARS | 61 | How long each chord lasts: 1 BAR, 2 BARS, HALF BAR. Kept when the voice changes |

**INPUT**

| Setting | CC | |
| - | - | - |
| GATE | 62 | Silence between words (0..100 %) |
| EVEN OUT | 63 | Soft and loud closer (0..100 %) |
| DE-ESS | 70 | Sharp s sounds softer (0..100 %) |
| HOWL GUARD | 71 | Stops the speaker ringing (0..100 %). Kept when the voice changes |
| MONITOR | 72 | Lower if the speaker rings (0..100 %). Kept when the voice changes |

**LOOP**

| Setting | CC | |
| - | - | - |
| LOOP | 73 | Whole bars, or any length: BARS, FREE. Kept when the voice changes |
| LOOP START | 74 | At your first sound, or rec: SOUND, PRESS. Kept when the voice changes |
| LOOP LEVEL | 75 | How loud the loop plays (0..100 %). Kept when the voice changes |
| LOOP KEEP | 76 | Less: old layers fade (0..100 %). Kept when the voice changes |
| TILT | 77 | What leaning forward does: LOOP SPEED, PITCH, FORMANT, OFF. Kept when the voice changes |
| SPEED MODE | 78 | The pitch when tilted: KEEP PITCH, OCTAVES, TAPE. Kept when the voice changes |
| ROLL | 79 | Left: echo  right: stutter: ECHO+STUTTER, OFF. Kept when the voice changes |
| CLEAR LOOP |  | Press to erase it |

**LINK**

| Setting | CC | |
| - | - | - |
| LINK |  | Sync with boards nearby: OFF, ON. Kept when the voice changes |
| GROUP |  | Same group: they sync (1..8). Kept when the voice changes |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR. Kept when the voice changes |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->
