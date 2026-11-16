# HUFF manual

## The screen

- Top line: HUFF, or the looper's state (REC, LOOP, ADD, STOP and the loop's length); the sound's name (a
  `*` once you have changed it); the output level. A padlock means the tilt is held. With LINK on, a small
  glyph before the meter: a blinking dot while no other board is heard, two boards that fill on the beat
  once linked and in step.
- Left: the instrument. The ocarina's holes open as the note climbs, low to high (the two small sub holes first,
  then the right hand, the left hand and the thumbs underneath); the flute's and the clarinet's open from the far
  end; the pan pipe's played tube fills; the trumpet's valves go down; the guitar's strings shake with the sound
  and a finger sits on the fret; the keyboard's key goes down and the wave above it opens with the breath; the
  choir's mouth opens with the breath. Air streams into the mouthpiece as you blow; notes float out of the window,
  swaying with the vibrato.
- Middle: the note, with an arrow while it is bent; the model, or OCTAVE UP / TWELFTH UP when it jumps; a bar
  for your breath with a mark where it jumps up.
- Right: the ladder of notes the tilt walks, the current one filled, a longer rung for the key's home note,
  and a pointer showing where the tilt is between rungs. With more notes than rows (over 14) the ladder shows a
  window that slides with the note; a small arrow at an end means more notes that way.
- Bottom: the looper, and the key and scale.
- On rev E, between the breath bar and the ladder: five marks for the octave the left knob has chosen, two down
  to two up. The longer mark is no shift, the filled one is where it is. Turn the knob and OCTAVE +1 (or -2, or 0)
  shows under the note for a second. A small arrow under the marks means PLAY > OCTAVE is lower than the knob goes.
- On rev E, while a hand is over the sensor: MUTE (or VIB, or BEND) and a bar for how far down the hand is, where
  the model's name was. With the mute, a hand comes down over the instrument and what is above it goes dim.

## The menu

The menu is a strip along the bottom of the screen; the instrument keeps playing while you are in it. Press the knob. Turn to choose a page, press to open it, press a setting to change it (turn), press again
to go back. Hold the knob to go back to playing. Press it twice quickly while you play and it turns the volume, until it
rests 3 s or you press it again. After you change a setting, turning the knob while you
play changes that setting; choose SOUND in the menu to make the knob change sounds again.

### SOUND

| | |
| - | - |
| SOUND | choose a sound |
| SAVE AS | store the current one as USER 1 to 8; it then comes after the factory sounds |
| REVERT | throw away your changes to this sound |

### PLAY

| | |
| - | - |
| MODEL | OCARINA, FLUTE, PAN PIPE, SHAKUHACHI, CLARINET, WHISTLE (an octave up), BOTTLE (an octave down), TRUMPET, TROMBONE (an octave down), DIDGERIDOO (three octaves down), GUITAR (an octave down), SYNTH LEAD, CHOIR (an octave down). See The models below |
| SCALE | the notes the tilt walks: pentatonics, major, minor, dorian, mixolydian, Hijaz, blues, In Sen, chromatic, phrygian, harmonic minor |
| KEY | the scale's home note |
| OCTAVE | shifts everything by octaves, -4 to +2: four down puts the ladder's home note at C1 (33 Hz). On rev E the left knob steps it between -2 and +2 |
| NOTES | how many notes on the ladder (3 to 24); fewer notes are easier to hit, 24 pentatonic notes are nearly five octaves |

### PITCH

| | |
| - | - |
| PITCH FROM | TILT; JOYSTICK (up and down walks the ladder); HUMMING (the note you hum, snapped to the scale) |
| SNAP | OFF: the tilt slides between notes instead of stepping |
| TILT SPAN | degrees from the lowest note to the highest (70 by default, up to 180: face down to face up) |
| GLIDE | how long a note takes to slide to the next |
| BEND RANGE | how far the joystick bends, in semitones |
| LEAN DOES | what leaning left or right does: VIBRATO, BEND or NOTHING |
| STICK UP/DN | the joystick up and down: VIBRATO (up adds it), BRIGHTNESS, or OCTAVE (push up or down for an octave) |
| A4 TUNING | 415 to 466 Hz |
| HAND DOES | rev E, a hand over the sensor: MUTE (darker and quieter the lower the hand), VIBRATO (the lower, the more, up to half a semitone each way), BEND UP or BEND DOWN (the lower, the further, up to BEND RANGE). It is the device's, not the sound's: it stays when you change sounds. On a board without the sensor it does nothing |

### BREATH

| | |
| - | - |
| SENSITIVITY | higher: a soft breath is already loud |
| CURVE | SOFT (quiet breath counts more), EVEN, HARD |
| GATE | how far above the room's noise a breath has to be (the room's own level is taken off the breath first) |
| ROOM NOISE | how strictly a quiet sound must be wind to count: its low band over its middle, against the ratio BREATH SETUP learned (50: a third of your breath's; 100: its square; 0: no test at all), how long it must keep coming before a note starts (0 to 40 ms), and how long after a voice-like sound it waits (0 to 100 ms). Anything as loud as half a breath counts regardless. Up in a loud room, down if your breath is missed |
| OVERBLOW AT | how hard you blow before a flute, the brass or the synth jumps an octave (the clarinet a twelfth); 100 = never |
| BREATH PITCH | a harder breath plays a little sharp, as on a real ocarina (in cents) |
| BREATH SETUP | stay quiet for two seconds, then blow normally for three: HUFF learns your mic, your breath's level over the room and (in a quiet room) its bands, so it can tell it from voices and music |

### TONE

| | |
| - | - |
| BREATHINESS | how much air you hear |
| CHIFF | the puff at the start of a note |
| BRIGHTNESS | dark to bright |
| VIBRATO | a wobble that is always there (leaning and the joystick add to it) |
| VIB RATE | wobbles per second |
| VIB DELAY | how long a note is held before the wobble starts |
| HARMONY | a second voice: a third or a fifth above, an octave up or down, or a drone on the home note or its fifth |
| HARM LEVEL | its level |
| DRIVE | the amp behind every model: 0 is a wire, 100 a fuzz (a soft clip with 41x in front, levelled after, then a cabinet's low pass) |

### SPACE

| | |
| - | - |
| REVERB | how much room |
| ROOM SIZE | from a small room to a cathedral |
| DAMPING | soft walls make a darker tail |
| ECHO | a repeat behind you |
| ECHO TIME | the time between repeats (changing it while it plays bends the echo, like tape) |
| ECHO BEATS | OFF: ECHO TIME counts. 1/8 to 1/2: the repeats fall on the shared tempo's beat (up to 1.5 s) |
| REPEATS | how long the echo lasts |

### SONG

| | |
| - | - |
| TEMPO | the tempo shared with the other boards (40 to 180 BPM). HUFF only uses it for ECHO BEATS; a board with a loop keeps the tempo |

KEY and SCALE (on PLAY) are shared the same way: change them here, or load a sound, and linked boards follow;
theirs change HUFF's. The other boards know six scales (major, minor, dorian, mixolydian, phrygian, harmonic
minor). HUFF's others tell them the one their notes sit in (a pentatonic its major or minor, blues minor, Hijaz
and In Sen phrygian) and stay as they are while the flock is on that one; CHROMATIC leaves the flock's scale alone.

### LINK

| | |
| - | - |
| LINK | ON: the radio is on and boards nearby in the same group share the tempo, the key and the scale |
| GROUP | 1 to 8: boards sync only within a group |
| RANGE | NEAR, ROOM, FAR: how far the radio reaches (and how much current it draws) |
| WHO IS THERE | shows the boards heard now |

### LOOP and SYSTEM

Shared by all the Samplooler instruments: loop level, loop keep (less than 100 % fades each pass),
clear; volume, speaker on or off, mic gain, tilt setup, motion, joystick, the REC button on or off, MIDI
channel and CC out, LED, hints, screen saver, the hand's sensor on or off, about, reset all. See `docs/CORE.md`.

## Rev E (the Vessel)

Rev E has no joystick. It has a second knob on the left, whose push is REC, and a sensor in the middle of the
board that measures how high a hand is held over it. The same firmware runs on both boards and finds out by
itself which one it is on. Nothing changes on a board with a joystick.

All of this is tested in the simulator only. No rev E board has run it, so the numbers below are the
simulator's, and how the hand feels is not known yet.

| | |
| - | - |
| left knob | one click is one octave, on top of the note the tilt picks. It stops at two octaves down and two up. It steps PLAY > OCTAVE itself, so it is saved with the other settings, shows as an edit of the sound, and a sound you load brings its own octave. If the menu has set OCTAVE to -3 or -4, a turn down leaves it and a turn up goes up one |
| a hand, HAND DOES: MUTE | a low pass over everything HUFF plays, after the amp and before the echo, the reverb and the looper (a loop that already plays is not muted; what you record through the mute is). The hand is read from 25 cm (nothing) to 4 cm (fully down). The cutoff comes down from 6 times the note to 1.2 times the note (350 Hz at least), so it closes over the note you play, high or low. Two thirds of the way down it has a resonance, which is the wah when you wave. Fully down the level is about 5 to 10 dB lower (up to 12 on a driven guitar) and what is above 3 times the note 17 dB lower or more, on every model |
| a hand, HAND DOES: VIBRATO | adds vibrato, as the joystick pushed up does: fully down is half a semitone each way |
| a hand, HAND DOES: BEND UP, BEND DOWN | bends the note, as the joystick left or right does: fully down is BEND RANGE. It goes out as MIDI pitch bend |

When the hand leaves, the mute, the vibrato or the bend lets go over about a quarter of a second. SYSTEM > HAND
OFF ignores the sensor.

What a rev E cannot do that a board with a joystick can:

- Bend and add vibrato from one control at the same moment. The hand does one of them (or the mute). Leaning
  left or right still does vibrato or a bend on both boards (PITCH > LEAN DOES), so lean for one and use the
  hand for the other.
- Bend up and down in one gesture with the hand. The hand bends one way, chosen in HAND DOES. Leaning bends
  both ways.
- PITCH FROM: JOYSTICK. Without a joystick the tilt picks the note.
- STICK UP/DN: BRIGHTNESS. The mute makes the tone darker than the setting, never brighter.
- STICK UP/DN: OCTAVE, the octave that lasts only while the stick is pushed. The left knob's octave stays.

## The models

| | |
| - | - |
| OCARINA, BOTTLE | a blown vessel: a sine with a little 2nd and 3rd, air noise rung through a band pass at the note, a chiff on the attack |
| FLUTE, PAN PIPE, SHAKUHACHI, WHISTLE | an air jet across a pipe (the STK flute: a jet delay, its cubic, a bore delay); the breath is a gain, blown hard the jet shortens and the note jumps an octave. Under about 100 Hz the jet has no stable note |
| CLARINET | a reed table on a half-length bore (the STK clarinet), odd harmonics; blown hard it jumps a twelfth |
| TRUMPET, TROMBONE, DIDGERIDOO | a lip (a two-pole resonance at the note) buzzing against the pressure in a bore two periods long (the STK brass); the lip picks the bore's mode, and blown hard it tightens an octave. Brass goes a little sharp when blown hard, as brass does. Under about 60 Hz it does not speak. The didgeridoo adds a vocal formant that opens with the breath |
| GUITAR | a string in a loop (Karplus-Strong), plucked when a note starts (CHIFF is the pick), fed a little noise while the breath lasts and clipped softly in the loop, so a held note swells to a level the breath sets and sings like a guitar on a sustainer; let go, it rings out in about 1.5 s. BRIGHTNESS is the string's loss filter. A new note while blowing slides (GLIDE) and gets a small pick |
| SYNTH LEAD | two saws a little apart and a square an octave down through a resonant low pass the breath opens; BRIGHTNESS moves the cutoff, blown hard it jumps an octave |
| CHOIR | a rounded glottal pulse with a little breath through three formants that move from OO to AH as the breath grows |

The breath itself: the mic's low band (wind on a MEMS mic is rumble; words and notes are mostly above 250 Hz) measured
over 30 ms, the room's floor taken off in power, a threshold over that; a gate that opens after ROOM NOISE's hold, and,
for a sound under half a full breath, only while its low band stands over the middle band (500 Hz to 4 kHz) by the
learned ratio and it has not just followed a voice-like sound (a hard breath is broadband at the mic's port, so it is
not asked); it closes when the breath is gone for 30 ms; then a breath-shaped smoother. A gate held open at under 12 %
for seconds is the room, and the floor learns it. HUMMING passes the tests (a voice is the point there). A breath sounds
about 25 ms after it starts at ROOM NOISE 50. On the board of 2026-10-22 a breath's low band read 0.1 (soft) to 1.8
(hard); the default breath range is 1.0 and BREATH SETUP sets it from yours.

## Tips

- Run BREATH SETUP in a quiet room once: it learns what your breath sounds like against the room. Done in a noisy
  room it learns the level and keeps the bands it had.
- Blow across the mic, not straight into it, and a little to the side: that is softer and steadier.
- A foam cover over the mic (a bit of a microphone windscreen) takes the pops out of a breath.
- Holding the knob keeps the note while you move the box around (to your mouth, say); hold again to let go.
- HUMMING follows your voice. With the speaker on it may follow its own sound: use headphones.
- The speaker playing into the mic does not count as breath (HUFF listens to the low rumble of your breath
  and leaves out the same band of its own sound), but very loud speakers close to the mic can still sustain
  a note. Turn the volume down, use headphones, or set SYSTEM > SPEAKER to OFF: the line out still plays.
- The LED shows the note as a colour around the scale; it flashes white when the note jumps up.
- On rev E, try the mute on TRUMPET or GUITAR SOLO first: bright sounds have the most to lose. On the ocarina
  and the bottle, which are close to a pure tone, the mute is mostly a drop in level.
- For a guitar solo: GUITAR SOLO, lean to bend, a little vibrato, and keep the breath on through a run: the notes
  slide into each other. Stop blowing and the last note rings out. For a synth solo, SYNTH SOLO does the same with
  GLIDE for the slides; ACID LEAD slides between every note.
- Four octaves down (PLAY > OCTAVE) the guitar is a bass and the choir a drone. The flutes stop speaking under about
  100 Hz and the brass under 60; the guitar, the synth, the choir, the ocarina and the bottle go all the way down.
