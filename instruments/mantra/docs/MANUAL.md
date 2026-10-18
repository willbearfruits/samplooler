# MANTRA manual

MANTRA is a singing voice made of formants: a seated Buddha who chants, sings overtones and sings "Daisy Bell",
with a wall of amps behind him when the sound calls for it. It sings by itself; your hands are its mouth and breath.
It can also put your own voice, from the mic, through the same mouth.

It owes two things. "Daisy Bell" (Harry Dacre, 1892) was the first song a computer sang, on an IBM 7094 at Bell Labs
in 1961. And the Delay Lama (AudioNerdz, early 2000s) was a free plugin with an animated monk singing vowels.
MANTRA is neither: its engine and its Buddha are its own.

## The knob: from the chant to Daisy

The knob blends the sound's chant into "Daisy Bell". The tune keeps running on the beat underneath, so turning the
knob never starts it again; each change comes in at the next bar.

| The knob | What you hear |
| - | - |
| 0 to 25 | the chant alone |
| 25 to 50 | the voices hum Daisy's melody over the drone, no words yet (in the throat sounds the whistle plays it) |
| 50 to 75 | the choir sings Daisy's words over the chant's drone |
| 75 to 100 | Daisy alone: one voice, a hummed waltz under it |

## The sounds

SOUND > SOUND in the menu. Each one is the chant at the knob's low end.

| Sound | What it sings |
| - | - |
| OM | one deep voice and its undertone an octave below: "Ommm", a breath, again |
| MONKS | five voices chanting OM together, a little apart, in a big hall |
| OM MANI | "Om mani padme hum" recited on one tone, the last word falling, with an echo |
| KHOOMEI | overtone singing: a drone, and one of its harmonics whistling a melody above it |
| SYGYT | the same, the whistle higher and sharper |
| KARGYRAA | the undertone growl: very low, the vowels moving slowly |
| DAISY | the chorus of "Daisy Bell", syllable by syllable, a hummed waltz under it |
| DAISY 8-BIT | the same in a rough 8-bit voice |
| VOWELS | one voice you play: the tilt is the vowel, the roll the pitch |
| HUM ALONG | hum into the mic: the voice follows your pitch, and your voice goes through the mouth too |
| SUNN | five monks chanting OM over the wall of amps: the fuzz wide open, feedback singing, a chord every four bars |
| DOOM DAISY | "Daisy Bell" slow over the amps; the amps are the waltz now |
| ORGAN | three monks over an organ's held chord |

## Your hands

| | |
| - | - |
| tip forward and back | the mouth: OO, OH, AH, EH, EE |
| roll left and right | the pitch, up to an octave each way (HANDS > ROLL: or the overtone) |
| turn it flat on the table | like a prayer wheel: the overtone walks up or down the harmonics (HANDS > YAW) |
| pump it up and down | the breath: louder, more air, an accent on each stroke (HANDS > PUMP) |
| stick left and right | bend |
| stick up / down | vibrato / the mouth shut to a hum (and the amps dive an octave) |
| knock on the box | strike the singing bowl (and hit the amps' chord, when DRONE > KNOCK is HIT) |
| turn the knob | from the chant to Daisy Bell |
| hold REC | loop what it sings; hold again to add; tap to stop or play |
| hold the knob | freeze the hands where they are |

## The mic

MIC > EFFECT puts the mic through the mouth: your voice takes the vowel the tilt gives it, with the undertone and the
overtone whistle of the sound. HUM ALONG turns it up and makes the voice follow your pitch. The speaker sits next to
the mic: use headphones or the line out, or SYSTEM > SPEAKER: OFF.

## The drone: a wall of amps

MIX > DRONE brings in a wall of amps behind the Buddha: one power chord held in the song's key through a fuzz and a
cab, a bass and a sub under it, the amp's hum, and feedback that grows over the chord until it sings. It is the amp
of WRECK's doom end, the same engine, so a MANTRA and a WRECK side by side make one sound. SUNN, DOOM DAISY and ORGAN
have it up; every other sound has an amp ready for when you turn MIX > DRONE up (OM a clean amp with an organ,
KARGYRAA a deep sub, DAISY a light clean guitar).

The chord follows the song's chords and changes on bar lines: DRONE > CHANGE says every bar, 2, 4 or 8 bars, or
never. 4 BARS is the pace of WRECK's DRONE zone, so linked boards move together there; set it to EVERY BAR for its
faster zones. The first chord swells in over SWELL; a new one slides in. Pumping the box opens the gain; a knock
hits the chord again (KNOCK: HIT); POUND hits it on bar lines by itself; the stick down sinks everything an octave.
GAIN, TONE, MIDS, SUB, FEEDBACK (at 50 it matches the chord's own 12th; from 85 up it sings over it), HUM, ORGAN,
TUNING (E STD down to DROP G) and SCALE (the song's notes, or a darker scale) are the amp's. MOD is a wah or a tremolo
on the song's beat.

## With a BAND, a SING or a WRECK

LINK is on by default, group 1. MANTRA takes the flock's tempo, key and chords: the chants sit on the key's root,
Daisy Bell is sung in the key, the echo is in time, the amps hold the chord the flock is on. With a WRECK on its doom
side, MANTRA on SUNN with CHANGE at 4 BARS is the same wall of amps on two boards.

## Every setting

The MIDI CC of a setting is in its table (the DRONE's are CC 68 to 85, in the slots after CHANT's).

<!-- tables -->
### VOICE

**VOICE**

| Setting | CC | |
| - | - | - |
| LEVEL | 20 | How loud the singers are (0..100 %) |
| TRACT | 21 | A child .. a giant (-100..100 %) |
| BREATH | 22 | Air in the voice (0..100 %) |
| ROUGH | 23 | A worn, creaky voice (0..100 %) |
| VIBRATO | 24 | How much a note wavers (0..100 %) |
| VIB RATE | 25 | How fast it wavers (3..8 Hz) |
| WHISTLE | 26 | Khoomei .. sygyt (0..100 %) |
| UNDERTONE | 27 | An octave below: kargyraa (0..100 %) |
| CHOIR | 28 | The singers spread (0..100 %) |
| 8-BIT | 29 | An old computer sings (0..100 %) |

**VOICE 2**

| Setting | CC | |
| - | - | - |
| BACKING | 30 | The others under the lead (0..100 %) |
| PRESSED | 31 | Lax .. pressed and bright (0..100 %) |
| NASAL | 32 | Singing through the nose (0..100 %) |
| RING | 33 | The opera singer's ring (0..100 %) |
| MOUTH SPEED | 34 | How fast the mouth moves (50..200 %) |
| GLIDE | 35 | Notes slide into the next (0..500 ms) |
| DRIFT | 36 | The pitch wanders a little (0..100 %) |
| BITS | 37 | 8-bit: how coarse (3..8) |
| RATE | 38 | 8-bit: its sample rate (4000..16000 Hz) |

### CHANT

**CHANT**

| Setting | CC | |
| - | - | - |
| MODE | 44 | What it sings: OM, OM MANI, THROAT, KARGYRAA, DAISY, VOWELS, HUM ALONG |
| OCTAVE | 45 | The singers up or down (-2..1) |
| PHRASE | 46 | Bars between breaths (1..8 bars) |
| MONKS | 47 | How many sing om (1..5) |
| DRONE | 48 | The low voice under it (0..100 %) |
| THROAT | 49 | Where the whistle sings: KHOOMEI, SYGYT |
| MELODY | 50 | The whistle's tune: STEPPE, RIVER, HORSE, HIGH, HANDS ONLY |
| BOWL STRIKES | 51 | When the bowl is struck: KNOCKS, PHRASES, BARS |

**CHANT 2**

| Setting | CC | |
| - | - | - |
| DAISY SPEED | 52 | One note of the tune is: 1/4 BEAT, 1/3 BEAT, 1/2 BEAT, 2/3 BEAT, 1 BEAT |
| WALTZ | 53 | The hummed oom-pah-pah (0..100 %) |
| HUM SNAP | 54 | On the song's notes: OFF, SCALE |
| BOWL NOTE | 55 | The bowl against the key (-12..12 st) |
| BOWL RING | 56 | How long the bowl rings (3..60 s) |
| MALLET | 57 | Soft felt .. hard wood (0..100 %) |

### DRONE

**DRONE**

| Setting | CC | |
| - | - | - |
| GAIN | 74 | Clean to wide open (0..100 %) |
| VOICE | 75 | Guitar, bass or both: GUITAR, BASS, BOTH |
| TUNING | 76 | How low it is tuned: E STD, DROP D, DROP C#, DROP C, B STD, DROP A, DROP G |
| CHANGE | 77 | When the chord moves: EVERY BAR, 2 BARS, 4 BARS, 8 BARS, NEVER |
| SWELL | 78 | How slow it swells in (0.5..12.0 s) |
| SUB | 79 | A sine an octave under (0..100 %) |
| FEEDBACK | 80 | Sings over the chord (0..100 %) |
| HUM | 81 | The amp's hum (0..100 %) |
| ORGAN | 82 | An organ under the amps (0..100 %) |

**DRONE 2**

| Setting | CC | |
| - | - | - |
| TONE | 83 | The fuzz: dark to bright (0..100 %) |
| MIDS | 84 | Scooped to pushed (0..100 %) |
| SCALE | 85 | The song's notes, or darker: SONG, MINOR, PHRYGIAN, HARM MINOR |
| KNOCK | 86 | A knock hits the chord: OFF, HIT |
| POUND | 87 | A hit on these bar lines: OFF, EVERY BAR, 2 BARS, 4 BARS |
| MOD | 88 | Wah or tremolo: OFF, WAH, TREMOLO |
| MOD DEPTH | 89 | How deep the mod is (0..100 %) |
| MOD RATE | 90 | Its cycle, on the song: 4 BARS, 2 BARS, 1 BAR, 1/2 BAR, 1 BEAT |
| SEND | 91 | Into the echo and the hall (0..100 %) |

### HANDS, MIC, FX, MIX, SONG, LINK

**HANDS**

| Setting | CC | |
| - | - | - |
| BLEND | 98 | 0 the chant, 100 daisy (0..100) |
| ROLL | 99 | What rolling it does: PITCH, OVERTONE, OFF |
| PITCH RANGE | 100 | How far a full roll goes (1..24 st) |
| YAW | 101 | The box turned flat: OVERTONE, PITCH, OFF |
| PUMP | 102 | Pumping it up and down: BREATH, OFF |
| KNOCKS | 103 | A knock strikes the bowl: OFF, ON |
| KNOCK SENSE | 104 | More: softer knocks count (0..100 %) |

**MIC**

| Setting | CC | |
| - | - | - |
| EFFECT | 105 | The mic through the mouth (0..100 %) |
| MIC GATE | 106 | Silence between sounds (0..100 %) |

**FX**

| Setting | CC | |
| - | - | - |
| ECHO | 107 | The chant comes back (0..100 %) |
| ECHO TIME | 108 | In time with the song: 1/8, 1/8 DOT, 1/4, 1/4 DOT, 1/2, 1 BAR |
| REPEATS | 109 | How long it goes on (0..95 %) |
| REVERB | 110 | How loud the hall is (0..100 %) |
| HALL SIZE | 111 | How long it rings (0.3..12.0 s) |
| SHIMMER | 112 | An octave up in the hall (0..100 %) |

**MIX**

| Setting | CC | |
| - | - | - |
| VOICES | 113 | The singers' level (0..100 %) |
| BOWL | 114 | The singing bowl's level (0..100 %) |
| DRONE | 115 | The wall of amps' level (0..100 %) |

**SONG**

| Setting | CC | |
| - | - | - |
| TEMPO | 116 | Shared with the flock (40..180 bpm) |
| KEY | 117 | The key they sing in: C, C#, D, D#, E, F, F#, G, G#, A, A#, B |
| SCALE | 118 | The notes of the key: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR |
| CHORDS | 119 | How the chords move by bar: HOLD, I V vi IV, vi IV I V, I vi IV V, I IV, ii V I, i VII VI V, i iv, 12 BAR BLUES |
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

## MIDI

Settings on the CCs in the tables above; program change picks a sound. REC is the sustain pedal (CC 64) or note 60.
