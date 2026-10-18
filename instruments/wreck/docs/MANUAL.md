# WRECK manual

WRECK is one machine with one knob that runs from breakcore to doom. At one end a break is chopped, rolled,
reversed and crushed at twice the tempo; at the other one chord is held under a slowed-down break, with feedback
and a huge room. In between: hardcore (a distorted kick, blast beats, a guitar chugging) and sludge (half time, fuzz
riffs). It plays by itself; you push it around with your hands.

## The knob: the axis

Turn the knob. The header shows where you are: the zone's name, a gauge with the four zones, the song's beat.

| zone | the knob | tempo (at the flock's 100 BPM) | what plays |
| - | - | - | - |
| BREAKCORE | 0 .. 25 | x2 (200) | the break chopped up: rolls, reversed slices, pitch jumps, odd bars, crushed |
| HARDCORE | 25 .. 50 | x1 (100) | a distorted kick on the beat, blast beats, the amp chugging on the kicks |
| SLUDGE | 50 .. 75 | x1/2 (50) | half time, the break pitched down and let ring, slow fuzz riffs |
| DRONE | 75 .. 100 | x1/4 (25) | one chord held, swelling, feedback over it, the break slowed into thunder |

The tempo only moves in those steps, so WRECK stays on the beat of a BAND or SING next to it. A new tempo comes in
on the song's next beat, after the tape brakes (WRECK > CHANGE: TAPE STOP) or on a clean cut (CUT). WRECK > TEMPO
STEPS picks which steps the two ends use (x2-x1/4, x1-x1/4, x2-x1/2, or STAY x1).

## Your hands

| | |
| - | - |
| turn the knob | the axis |
| press the knob | the menu |
| hold the knob | the step grid (below) |
| tip forward | chaos: more edits, more mess |
| tip back | drag: the tape slows down; level again, it catches up and is back in time |
| roll left | reverse and smear |
| roll right | stutter: the last bit repeats, shorter the further you roll |
| stick up | a roll on the slice playing, faster and rising the further |
| stick down | dive: the break and the amp drop, down to an octave |
| stick left / right | crush / drive |
| knock on the box | a fill (longer when harder); towards DRONE a chord hit |
| hold REC (0.3 s or more) | sample a new break (below) |
| tap REC | stop (the tape brakes to a halt, STOP in the header); tap again to start (it spins up) |

REC is the BOOT button on the board when WRECK is new: SYSTEM > REC BUTTON starts OFF, so the REC pin (on the kit,
the joystick's click) does nothing until you turn it on. BOOT is a REC button on every board either way, and so are
a sustain pedal (MIDI CC 64) and MIDI note 60. A REC that is already down when WRECK starts counts for nothing until
it has been let go.

SYSTEM has the core's settings: volume, speaker, mic gain, tilt setup, motion, the joystick, the REC button, MIDI
channel and CC out, the LED, hints, the screen saver, ABOUT and RESET ALL.

## The breaks

There is no recording of anyone else's break in WRECK. The built-in breaks are played by a synthesized drum kit (an
acoustic kit with its room, printed a little rough, as off a record) in the classic patterns, two bars each:
KIT > BREAK picks AMEN (136 BPM), THINK (115), APACHE (118, with bongos), FUNKY (100), IMPEACH (98), BOOM BAP (90),
BLAST (110) or DOOM (72, slow toms and crashes). They are played like a drummer would (ghost notes, swing, every hit
a little different: KIT > SWING, GHOSTS, HUMAN; TAKE plays another performance of the same break) and printed
through KIT > PRINT: CLEAN, TAPE, VINYL (crackle), 12-BIT, DUSTY. A new break is printed in the background while
the old one plays on (the screen shows a bar filling); on the board that takes a few seconds, longest for DOOM.

To use a real one, sample it:

1. SAMPLE > SAMPLE FROM: MIC (anything the board hears), USB (what the computer plays: the board is a USB sound card;
   USB THRU lets you hear it), or RESAMPLE (WRECK's own output: chop it, resample it, chop it again).
2. Hold REC while it plays and let go. REC counts as held after 0.3 s (the header shows REC); a shorter press is a
   tap, which stops or starts the machine and records nothing. The screen says WAITING FOR A SOUND until something
   is heard: let go before that and nothing changes (NOTHING HEARD: NO BREAK). The take starts at its first sound
   (REC's own click is left out; RESAMPLE starts on WRECK's step instead), is trimmed to whole beats at WRECK's
   tempo (KIT 2 > LENGTH: AUTO) or to 1, 2 or 4 bars (the tempo then comes from the take), and cut into slices at
   its hits (SLICES: AUTO, or 8, 16, 32 even slices). A take is 8 seconds at most (up to 16 beats): at 8 s it ends
   by itself, and REC does nothing more until it has been let go.
3. It plays at once; KIT > BREAK shows SAMPLED. Pick a built-in break to go back. KIT 2 > GATE sets how loud the
   first sound must be. The take is not kept when WRECK is switched off: it starts with a built-in break again.

While the step grid is open, REC belongs to the grid (below) and does not sample.

## The step grid

Hold the knob (or BEAT > EDIT GRID): the pattern's 16 steps in two rows of 8, each showing what it plays (K kick, S
snare, R ride ...) and its trick. Opening it sets BEAT > PATTERN to MINE; AUTO gives the pattern back to the axis.

| | |
| - | - |
| turn | the step |
| press and turn | its slice: the break's own, one of its slices, or off |
| press | the next trick: reverse, rolls x2, x3, x4, x8, x16, x64, up, down, +7, dive, rise, cut, tape stop, reversed roll |
| hold | close the grid (the knob is the axis again) |
| REC tap | stop / run the grid (stopped, you hear each slice as you pick it) |
| REC held 1 s | back to the break's own order |
| stick | left and right the step, up and down the trick |

The grid is saved with a sound.

## The amp and the effects

The AMP is a downtuned guitar and bass in one fuzzed voice: power chords on the song's chord, in AMP > TUNING (E
standard down to drop G), in the song's scale or a darker one (AMP > SCALE: MINOR, PHRYGIAN, HARM MINOR). At the
BREAKCORE end it is silent, stabs or chugs (AMP > BREAKCORE); at HARDCORE it chugs on the kicks or tremolo picks
(CHUGS, TREM PICK); at SLUDGE it plays one of its riffs (RIFF); at DRONE it holds one chord, swelling in (SWELL), with
feedback singing over it (FEEDBACK: at 50 it only matches the chord's own 12th; from 85 up it sings over it) and the
amp's hum (HUM). ORGAN and HARPSI turn it into an organ or a harpsichord-like line; MOD is a wah or a tremolo on the
grid.

The FX: on the drums, CRUSH (bits and rate), DRIVE (WARM, TORN, FOLD) and a FILTER; DARK closes a low pass towards
DRONE, DOOM CRUSH and DOOM DRIVE are what the doom end keeps of them. On everything: the tape (WOW and FLUTTER grow
towards DRONE; DRAG is how far tipping back slows it; BRAKE how long a stop takes), then the reverb: ROOM at the
BREAKCORE end, HALL (up to 15 s) at DRONE.

## Sounds

SOUND > SOUND. Each sets both ends of the axis:

| sound | at the breakcore end | at the doom end |
| - | - | - |
| WRECK | the Amen pattern chopped, gabber at HARDCORE | fuzz riffs, a held chord |
| SNARES | bars of 7, snare rushes, rolls rising in pitch | a slow, cleaner amp |
| DRILL | micro rolls and pitch sweeps on a tight funk break | half time |
| GABBA | a distorted kick on every beat, everything driven | a slow industrial pound |
| GRIND | blast beats, the amp tremolo picking | sludge |
| BAROQUE | breakcore and metal with a harpsichord-like line | an organ-like drone |
| WIZARD | a stoner break | fuzz riffs, wah, half time, a lot of reverb |
| EARTH | a dry, sparse break | slow, cleaner tremolo guitar, sparse heavy drums |
| DRONE | noise breaks | one chord forever, feedback, the amp's hum |
| THUNDER | the break up close | the break slowed into thunder, a huge room |

## With a BAND or a SING

LINK is on by default, group 1. WRECK takes the flock's tempo, key and chords (and gives its own when changed here);
its tempo is the flock's x2 down to x1/4, on the same beat. The AMP plays in the song's key; AMP > SCALE can darken
it (MINOR, PHRYGIAN) without changing the flock's.

## Every setting

<!-- tables -->
### WRECK

**WRECK**

| Setting | CC | |
| - | - | - |
| AXIS |  | 0 breakcore, 100 drone (0..100) |
| TEMPO STEPS |  | The tempo at each end: x2-x1/4, x1-x1/4, x2-x1/2, STAY x1 |
| CHANGE |  | How a new tempo comes in: TAPE STOP, CUT |
| KNOCKS |  | A knock is a fill: OFF, ON |
| KNOCK SENSE |  | More: softer knocks count (0..100 %) |

### BEAT

**BEAT**

| Setting | CC | |
| - | - | - |
| LEVEL | 44 | How loud the break is (0..100 %) |
| EDITS | 45 | How chopped up it gets (0..100 %) |
| ROLLS | 46 | Rolls and snare rushes (0..100 %) |
| REVERSE | 47 | Slices played backwards (0..100 %) |
| DRILL | 48 | Micro rolls and sweeps (0..100 %) |
| PITCH | 49 | Pitch of the break (-12..12 st) |
| SPEED | 50 | Tied: slows with the tempo: SLICE, TIED |
| ODD BARS | 51 | Odd bars against the 4/4: OFF, 7/8, 5/8, 9/8, AUTO |
| PATTERN | 52 | Mine: the grid you drew: AUTO, MINE |
| EDIT GRID |  | Chop it yourself: 16 steps |

**BEAT 2**

| Setting | CC | |
| - | - | - |
| GABBER KICK | 54 | A hard kick on every beat: OFF, ON, AUTO |
| KICK TUNE | 55 | The low note of the kick (35..90 Hz) |
| KICK DIRT | 56 | How hard the kick clips (0..100 %) |
| BLAST | 57 | Blast beats at hardcore (0..100 %) |
| SWING | 58 | Offbeat sixteenths late (0..100 %) |
| DOOM PITCH | 59 | Pitched down towards doom (-12..0 st) |
| SEND | 60 | The break into the reverb (0..100 %) |

### AMP

**AMP**

| Setting | CC | |
| - | - | - |
| LEVEL | 74 | How loud the amp is (0..100 %) |
| GAIN | 75 | Clean to wide open (0..100 %) |
| VOICE | 76 | Guitar, bass or both: GUITAR, BASS, BOTH |
| TUNING | 77 | How low it is tuned: E STD, DROP D, DROP C#, DROP C, B STD, DROP A, DROP G |
| SCALE | 78 | The song's notes, or darker: SONG, MINOR, PHRYGIAN, HARM MINOR |
| BREAKCORE | 79 | What it plays at breakcore: SILENT, STABS, CHUGS |
| CHUGS | 80 | How many kicks it chugs on (0..100 %) |
| TREM PICK | 81 | Bars of tremolo picking (0..100 %) |
| PICK RATE | 82 | How fast it tremolo picks: 1/16, 1/32, 1/64 |
| RIFF | 83 | The slow riff at sludge: TRITONE, CREEP, SMOKE, BENDS, TWANG, POUND |
| DRONE | 84 | Auto: a held chord at drone: OFF, ON, AUTO |
| HARPSI | 85 | A harpsichord arpeggio (0..100 %) |

**AMP 2**

| Setting | CC | |
| - | - | - |
| TONE | 86 | The fuzz: dark to bright (0..100 %) |
| MIDS | 87 | Scooped to pushed (0..100 %) |
| SUB | 88 | A sine an octave under (0..100 %) |
| FEEDBACK | 89 | Sings over the drone (0..100 %) |
| HUM | 90 | The amp's hum at drone (0..100 %) |
| SWELL | 91 | How slow the drone swells (0.5..12.0 s) |
| ORGAN | 92 | An organ under the drone (0..100 %) |
| MOD | 93 | Wah or tremolo: OFF, WAH, TREMOLO |
| MOD DEPTH | 94 | How deep the mod is (0..100 %) |
| MOD RATE | 95 | Its speed, on the beat: 1 BAR, 1/2 BAR, 1/4, 1/8, 1/16 |
| SEND | 96 | Into the reverb (0..100 %) |

### FX

**FX**

| Setting | CC | |
| - | - | - |
| CRUSH | 98 | Fewer bits: 16 down to 4 (0..100 %) |
| RATE | 99 | Lower rate, down to 3 khz (0..100 %) |
| DRIVE | 100 | Distortion at hardcore (0..100 %) |
| DRIVE TYPE | 101 | Warm, torn or folded: WARM, TORN, FOLD |
| FILTER | 102 | The cutoff; at the end: off (40..16000 Hz) |
| RESONANCE | 103 | A peak at the cutoff (0..100 %) |
| FILTER TYPE | 104 | Low pass or high pass: LOW PASS, HIGH PASS |
| DARK | 105 | Darker drums at drone (0..100 %) |
| DOOM CRUSH | 106 | The crush left at drone (0..100 %) |
| DOOM DRIVE | 107 | The drive left at drone (0..100 %) |

**FX 2**

| Setting | CC | |
| - | - | - |
| ROOM | 108 | Room decay at breakcore (0.2..4 s) |
| HALL | 109 | Cathedral decay at drone (2..15 s) |
| REVERB | 110 | How loud the room is (0..100 %) |
| DAMP | 111 | A darker, softer tail (0..100 %) |
| WOW | 112 | Slow tape wobble at drone (0..100 %) |
| FLUTTER | 113 | Fast tape wobble at drone (0..100 %) |
| DRAG | 114 | Tape speed tipped back (0..100 %) |
| BRAKE | 115 | How long a stop takes (25..200 %) |
| GLUE | 116 | Soft saturation on all (0..100 %) |

### KIT

**KIT**

| Setting | CC | |
| - | - | - |
| BREAK | 20 | The break it chops: AMEN, THINK, APACHE, FUNKY, IMPEACH, BOOM BAP, BLAST, DOOM, SAMPLED |
| PRINT | 21 | Tape, a record, a sampler: CLEAN, TAPE, VINYL, 12-BIT, DUSTY |
| ROOM | 22 | The room it was played in (0..100 %) |
| TUNE | 23 | The drums up or down (-12..12 st) |
| TONE | 24 | Dark or bright (0..100 %) |
| DECAY | 25 | Muffled or ringing drums (0..100 %) |
| SWING | 26 | Later offbeats (0..100 %) |
| GHOSTS | 27 | Soft snare notes (0..100 %) |
| HUMAN | 28 | How loose the drummer is (0..100 %) |
| TAKE | 29 | Another take of it (1..9) |

**KIT 2**

| Setting | CC | |
| - | - | - |
| LENGTH | 30 | Whole beats, or bars: AUTO, 1 BAR, 2 BARS, 4 BARS |
| SLICES | 31 | At its hits, or equal parts: AUTO, 8, 16, 32 |
| GATE | 32 | What starts a take (-60..-12 dB) |

### SAMPLE, MIX, SONG, LINK

**SAMPLE**

| Setting | CC | |
| - | - | - |
| SAMPLE FROM |  | What holding rec records: MIC, USB, RESAMPLE |
| USB THRU |  | Hear the computer (usb): OFF, ON |

**MIX**

| Setting | CC | |
| - | - | - |
| DRUMS |  | The break's level (0..100 %) |
| AMP |  | The amp's level (0..100 %) |

**SONG**

| Setting | CC | |
| - | - | - |
| TEMPO |  | Shared with the flock (60..180 bpm) |
| KEY |  | The key the amp plays in: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE |  | The notes of the key: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| CHORDS |  | How the chords move by bar: HOLD, I V vi IV, vi IV I V, I vi IV V, I IV, ii V I, i VII VI V, i iv, 12 BAR BLUES |
| HOLD CHORD |  | The chord for chords: hold: I, II, III, IV, V, VI, VII |
| CHORD BARS |  | How long each chord lasts: 1 BAR, 2 BARS, HALF BAR |

**LINK**

| Setting | CC | |
| - | - | - |
| LINK |  | Sync with boards nearby: OFF, ON |
| GROUP |  | Same group: they sync (1..8) |
| RANGE |  | How far the radio reaches: NEAR, ROOM, FAR |
| WHO IS THERE |  | The boards linked now |
<!-- /tables -->

## The screen and the LED

The header: the zone (REC while sampling, ... while a break prints, STOP when stopped), the axis as a gauge with the
four zones, the song's beat (four dots, the bar's first bigger), the link (two bars that fill on the beat once a
board is linked and in time; a blinking dot while it looks for one) and the level. Along its bottom edge, the pattern's steps
and the one playing. The scene is the break's waveform with its slices at the breakcore end (the slice playing lit
and jolted by the edits), the amp stack in smoke at the doom end; the break's name and the machine's tempo top left.

The LED runs from hot pink at BREAKCORE to a dim red at DRONE, brighter on the beat; red while sampling; nearly off
when stopped. SYSTEM > LED sets how bright it is.

## MIDI

On the channel set in SYSTEM > MIDI CH, both ways. In: the settings on the CCs in the tables above (those without a
number have no CC: the WRECK, SAMPLE, MIX, SONG and LINK pages, so the axis is the knob's only); a program change
picks a sound in the list's order (the ten, then any saved with SAVE AS); CC 64 (a sustain pedal) and note 60 are
REC; note 64 holds and frees the motion. Out: a CC when a setting is changed on the device, a program change on a sound change,
and with SYSTEM > MIDI CC OUT on the lean (CC 12, 13) and the stick (CC 14, 15).

## When something is wrong

- REC does nothing: SYSTEM > REC BUTTON is OFF (as it is when WRECK is new). Use BOOT, or turn it on. REC held while
  WRECK starts is ignored until it has been let go.
- The take ends before you let go: a take is 8 seconds at most. Let go of REC before it counts again.
- NOTHING HEARD: NO BREAK: the sound was under KIT 2 > GATE, or the source is not the one playing (SAMPLE FROM).
- Knocks are missed, or it fills by itself when you carry it: WRECK > KNOCK SENSE, or KNOCKS: OFF.
- Sampling from USB is silent: the computer must play to the board's sound card (WRECK in the sound settings).
- The tempo will not change: a loop on a linked board keeps it (A LOOP KEEPS THE TEMPO). SONG > TEMPO follows the flock.
- Back to new: SYSTEM > RESET ALL (the REC-at-power-on reset of the core needs REC BUTTON on).
