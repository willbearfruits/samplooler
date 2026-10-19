# HUFF manual

## The screen

- Top line: HUFF, or the looper's state (REC, LOOP, ADD, STOP and the loop's length); the sound's name (a
  `*` once you have changed it); the output level. A padlock means the tilt is held. With LINK on, a small
  glyph before the meter: a blinking dot while no other board is heard, two boards that fill on the beat
  once linked and in step.
- Left: the ocarina. Its holes open as the note climbs, low to high (the two small sub holes first, then the
  right hand, the left hand and the thumbs underneath). Air streams into the mouthpiece as you blow; notes
  float out of the window, swaying with the vibrato.
- Middle: the note, with an arrow while it is bent; the model, or OCTAVE UP / TWELFTH UP when it jumps; a bar
  for your breath with a mark where it jumps up.
- Right: the ladder of notes the tilt walks, the current one filled, a longer rung for the key's home note,
  and a pointer showing where the tilt is between rungs.
- Bottom: the looper, and the key and scale.

## The menu

Press the knob. Turn to choose a page, press to open it, press a setting to change it (turn), press again
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
| MODEL | OCARINA, FLUTE, PAN PIPE, SHAKUHACHI, CLARINET, WHISTLE (an octave up), BOTTLE (an octave down) |
| SCALE | the notes the tilt walks: pentatonics, major, minor, dorian, mixolydian, Hijaz, blues, In Sen, chromatic, phrygian, harmonic minor |
| KEY | the scale's home note |
| OCTAVE | shifts everything by octaves |
| NOTES | how many notes on the ladder (3 to 15); fewer notes are easier to hit |

### PITCH

| | |
| - | - |
| PITCH FROM | TILT; JOYSTICK (up and down walks the ladder); HUMMING (the note you hum, snapped to the scale) |
| SNAP | OFF: the tilt slides between notes instead of stepping |
| TILT SPAN | degrees from the lowest note to the highest (70 by default) |
| GLIDE | how long a note takes to slide to the next |
| BEND RANGE | how far the joystick bends, in semitones |
| LEAN DOES | what leaning left or right does: VIBRATO, BEND or NOTHING |
| STICK UP/DN | the joystick up and down: VIBRATO (up adds it), BRIGHTNESS, or OCTAVE (push up or down for an octave) |
| A4 TUNING | 415 to 466 Hz |

### BREATH

| | |
| - | - |
| SENSITIVITY | higher: a soft breath is already loud |
| CURVE | SOFT (quiet breath counts more), EVEN, HARD |
| GATE | how far above the room's noise a breath has to be |
| OVERBLOW AT | how hard you blow before a flute jumps an octave (the clarinet a twelfth); 100 = never |
| BREATH PITCH | a harder breath plays a little sharp, as on a real ocarina (in cents) |
| BREATH SETUP | stay quiet for two seconds, then blow normally for three: HUFF learns your mic and your breath |

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
channel and CC out, LED, hints, screen saver, about, reset all. See `docs/CORE.md`.

## Tips

- Blow across the mic, not straight into it, and a little to the side: that is softer and steadier.
- A foam cover over the mic (a bit of a microphone windscreen) takes the pops out of a breath.
- Holding the knob keeps the note while you move the box around (to your mouth, say); hold again to let go.
- HUMMING follows your voice. With the speaker on it may follow its own sound: use headphones.
- The speaker playing into the mic does not count as breath (HUFF listens to the low rumble of your breath
  and leaves out the same band of its own sound), but very loud speakers close to the mic can still sustain
  a note. Turn the volume down, use headphones, or set SYSTEM > SPEAKER to OFF: the line out still plays.
- The LED shows the note as a colour around the scale; it flashes white when the note jumps up.
