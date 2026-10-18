# MANTRA: how the builders share the work

The user asked (2026-10-17) for "a formant synth, inspired by Daisy Bell, and the Buddha plugin for Cubase SX"
(that is the Delay Lama by AudioNerdz: a free VST from the early 2000s, an animated monk singing vowels on MIDI
notes, with a delay), "the Buddha as the animation protagonist", "monk singing om and overtone singing, as well as
Daisy Bell using SAM maybe", "it's also an effect but mostly a synth", "make use of the tilt, yaw, pumping maybe,
joystick". Read `firmware/main/mantra.h` first: it is the contract.

SAM (Software Automatic Mouth, 1982) is not used: the C code that circulates is a reverse-engineered copy of a
commercial program whose owner never released it. The 8-bit robot (DAISY 8-BIT) is our own engine's mode. Daisy Bell
(Harry Dacre, 1892) is public domain; the IBM 7094 performance (1961, Kelly, Lochbaum, Mathews) is the inspiration,
not something copied.

## The parts

| file | owner | what |
| - | - | - |
| `mantra.h`, `instrument.c`, `inst_scene.c`, `tools/sim/test_inst.c` | host (lead) | the contract, params, sounds, link, hands, mix, FX, screen |
| `inst_voice.c`, `inst_voice.h`, `tools/sim/test_voice.c` (+ a harness of your own) | VOICE | the formant voices, phonemes, overtone, undertone, choir, 8-bit, the mic effect, pitch tracking |
| `inst_score.c`, `inst_bowl.c`, `inst_score.h`, `tools/sim/test_score.c` | SCORE | the modes' sequencing, Daisy Bell, the chants, the throat melodies, the singing bowl |
| `inst_drone.c`, `inst_drone.h` (the API is the lead's), `tools/sim/test_drone.c` | DRONE | the wall of amps: WRECK's AMP at its doom end, held chords in the song's key, swells, feedback, sub, hum, organ |
| `inst_fx.c` | SING's | the echo and reverb bus, unchanged |

The placeholders in `inst_voice.c` and `inst_score.c` only keep the build going: replace yours whole.

## The sounds (the knob walks them; each module fills `sounds[MN_NSOUNDS]` with its own tags)

| # | name | mode | what it should be |
| - | - | - | - |
| 0 | OM | OM | one deep voice (the key's root, two octaves down: about 65-100 Hz) and its undertone an octave below, "OW" opening into "M", a breath between phrases, slow |
| 1 | MONKS | OM | five voices: unison, slightly apart in pitch and time, one an octave below; a big hall (the host's) |
| 2 | OM MANI | MANI | OM MA NI PAD ME HUM recited on one tone in rhythm, falling at the phrase's end, a drone under it, the Delay Lama's echo |
| 3 | KHOOMEI | THROAT | a drone (about 110-150 Hz) and the whistle singing a melody on its harmonics (6..12), as Tuvan khoomei |
| 4 | SYGYT | THROAT | the same, the whistle sharp and high (harmonics 8..16), the drone quieter |
| 5 | KARGYRAA | KARGYRAA | the undertone growl: very low, period doubled, the vowels moving slowly (OO OH AH) |
| 6 | DAISY | DAISY | the chorus of Daisy Bell, sung syllable by syllable, a little robotic, as the IBM 7094; a hummed oom-pah-pah waltz under it |
| 7 | DAISY 8-BIT | DAISY | the same in the 8-bit robot voice |
| 8 | VOWELS | VOWELS | the Delay Lama's way: one voice, the jaw is the vowel, roll the pitch, the stick bends; pumping the breath |
| 9 | HUM ALONG | HUM | the voice follows the mic's pitch (mv_track), the jaw's vowel; the mic also through the mouth (the host's EFFECT) |
| 10 | SUNN | OM | (2026-10-18) five monks' OM over the wall of amps: the fuzz wide open, DROP A, the sub, the feedback singing, the hum, a 6 s swell, the chord changing every 4 bars, a 10 s hall |
| 11 | DOOM DAISY | DAISY | Daisy Bell slow (DAISY SPEED slower, WALTZ 0) over the drone: the waltz is the amps now |
| 12 | ORGAN | OM | three monks over an organ's held chord: ORGAN up, the gain low, no feedback, no hum; a church |

## The hands (`hands_t`, the host fills it every tick)

- tilt forward and back: `jaw` (0 OO .. 0.5 AH .. 1 EE)
- roll: `roll` -1..1 (HANDS > ROLL: the pitch, or the overtone; the host adds the pitch bend itself)
- yaw: `yaw` (turns, cumulative, clamped to +-2) and `yaw_rate` (HANDS > YAW: the overtone by default; the score
  walks the whistle up and down the harmonics with it, like a prayer wheel)
- pumping: `pump` 0..1 (the breath) and `pumped` (a stroke this tick: an accent, or the next syllable)
- the stick: `jx` (bend, the host adds it), `jy` (up: vibrato, the host's; down: the hum, the host's)
- a knock: `knocked`, `knock`: the host calls `bowl_strike()`

## Daisy Bell's chorus (Harry Dacre, 1892): G major, 3/4, in quarter-note beats from the chorus' start

Taken from a MIDI transcription of the public-domain tune; a few syllables split as sung. `beat length note syllable`:

```
0 3 D5 Dai | 3 3 B4 sy | 6 3 G4 Dai | 9 3 D4 sy | 12 1 E4 give | 13 1 F#4 me | 14 1 G4 your | 15 2 E4 an | 17 1 G4 swer
18 5 D4 do | 24 3 A4 I'm | 27 3 D5 half | 30 3 B4 cra | 33 3 G4 zy | 36 1 E4 all | 37 1 F#4 for | 38 1 G4 the
39 2 A4 love | 41 1 B4 of | 42 5 A4 you | 47 1 B4 It | 48 1 C5 won't | 49 1 B4 be | 50 1 A4 a | 51 2 D5 sty | 53 1 B4 lish
54 1 A4-B4-A4 mar (a turn: three triplet notes on one syllable) | 55 4 G4 riage | 59 1 A4 I | 60 2 B4 can't | 62 1 G4 af
63 2 E4 ford | 65 1 G4 a | 66 1 E4 car | 67 4 D4 riage | 71 1 D4 But | 72 2 G4 you'll | 74 1 B4 look | 75 3 A4 sweet
78 2 G4 up | 80 0.5 B4 on | 80.5 0.5 B4 the | 81 2 A4 seat | 83 0.5 B4 of | 83.5 0.5 C5 a | 84 1 D5 bi | 85 1 B4 cy
86 1 G4 cle | 87 2 A4 built | 89 1 D4 for | 90 6 G4 two
```

96 beats, 32 bars. The words: "Daisy, Daisy, give me your answer, do! I'm half crazy, all for the love of you! It
won't be a stylish marriage, I can't afford a carriage, but you'll look sweet upon the seat of a bicycle built for
two." Transpose it to the song's key (it is written in G; `song.key`), its quarter on the song's beat at a speed of
your choosing (the default song tempo is 72: a quarter at two per beat is a waltz).

## The DRONE (added 2026-10-18: "elements of Sunn O))) and heavy drones: Buddha, drone, Daisy, reverb, delay; it should
## fit perfectly with WRECK, especially on the doom side, but on all the spectrum")

`inst_drone.h` is the contract: `dr_set(dr_ctl_t)` every tick from the host (the breath = swell, the stick's bend and
dive, a knock = a chord hit if the module's KNOCK says so, running, MIX > DRONE as `level`, the send), `dr_process(out,
send, n)` adds the amps, `dr_view` for the screen. The module (`mod_drone`, letter 'd', pages DRONE and DRONE 2, at
most 24 params) owns the sound: GAIN, VOICE (GUITAR / BASS / BOTH), TUNING (as WRECK's: E STD .. DROP G), TONE, MIDS,
SUB, FEEDBACK, HUM, SWELL, ORGAN, SEND, CHANGE (the chord: EVERY BAR / 2 BARS / 4 BARS / 8 BARS / NEVER; 4 BARS is
WRECK's DRONE zone, where the machine's bar is four song bars), KNOCK (OFF / HIT: a knock re-strikes the chord), and
whatever else the doom needs (a slow pound on the beat? a wah / tremolo as WRECK's MOD?). Port WRECK's engine
(instruments/wreck/firmware/main/inst_amp.c: the strings, the fuzz at 2x between halfbands, the tone stack, the cab,
the bass and the sub, the hum, the feedback partial that climbs, the organ, the ramps) and only its drone behaviour:
no chugs, no tremolo picking, no riffs, no harpsichord, no machine grid. The chord is `song.chord` / `scale_note` from
mantra.h, taken on the song's bar lines per CHANGE (`song_beats_at`, `song_frame_of`); a new chord slides in; the first
chord swells in from silence over SWELL; the breath opens the swell and the gain a little; the dive bends down an
octave. Keep WRECK's voicing and levels so the two boards sound like one amp wall when linked (same key, same chords on
the link). Budget: DRONE <= 5 us per block on the host; PSRAM <= 64 KB; internal RAM for anything read per sample.
The drone must be silent and free (no cost beyond a few checks) while MIX > DRONE is 0 or nothing is held.

- Work in your own copy: `SP=/tmp/claude-1000/-home-glitchesom-Projects/1b204ff0-7c0b-4db6-808c-66158f06d8bd/scratchpad`,
  `rsync -a --exclude 'firmware/build*' --exclude firmware/sdkconfig --exclude 'tools/sim/obj' --exclude 'tools/sim/out' --exclude tools/sim/sim ~/Projects/samplooler/samplooler/instruments/mantra/ $SP/mantra-<you>/`.
  Build and test there (`tools/sim/run.sh`, `tools/sim/run.sh inst`). Before your final run, rsync the real folder's
  other files into your copy again (keep your own), run the whole simulator, then copy only your own files back.
- You may not start other agents. Touch only your own files; anything you need from the host or the other builder goes
  under "Requests" below and in your report. Do not edit `mantra.h`, `instrument.c`, `inst_scene.c`, `test_inst.c`.
- Nothing flashed, committed, pushed or published. At the end one device build in your copy:
  `. ~/esp/esp-idf/export.sh && cd firmware && idf.py -B build-<you> build` (warnings are errors), then delete that build
  directory.
- The audio rules in `mantra.h` and `docs/CORE.md`. Budgets on the host per 64-frame block: VOICE <= 8 us with six
  voices singing, SCORE + bowl <= 1. Measure yours in your test and report it.
- Params: at most 24 (`MOD_PARAMS_MAX`), on your two pages, tags starting with your letter ('v' VOICE, 'c' SCORE, 'd' DRONE),
  names and help short, in capitals (the simulator checks they fit). Coach marks: the host uses `LEARN_INST` + 0..3;
  VOICE + 8..11, SCORE + 12..15, DRONE + 16..19.
- Your test: `void test_mod_<you>(void)` in `tools/sim/test_<you>.c`. Put back any `SI` field you change. WAVs to listen
  to (`sim_wav("-<you>-...", t0, t1)`): the user judges by ear, and is away now: make the WAVs worth it.
- Copy for people is plain and short. Report: what you built, params and tags, tests and results, cost and memory,
  requests. Short.

## Requests

(builders: add yours here, with your name and the date)

VOICE, 2026-10-18:
- host: in HUM ALONG call `mv_effect` every block, also with EFFECT at 0 (amount 0: it only listens, about 1 us);
  `mv_track` is only fresh while it is called.
- host: the IRAM fragment's entry points for VOICE are `inst_voice:mv_process inst_voice:mv_effect` (tools/iram_lf.py:
  9 functions, 40 KB of code, 7 tables; sqrtf, log2f, cosf, sinf are called once a block or on a change only).
- SCORE: the whistle (`overtone`) is the lead's alone (voice 0): in THROAT the whistling drone is voice 0, other drones
  are plain. `hum` shuts every voice to M; `sub`, `c64` and the rest add to VOICE's params (DAISY 8-BIT's 8-bit is
  VOICE's `v8bt=100`: the score need not set `c64`).
- SCORE: give consonants `mv_ph_len()` (a stop's includes its closure, burst and aspiration) and queue a syllable whole
  (only its first event with an `at`): a stop and H look at the next queued phoneme to move to it, and a vowel's end
  moves toward the consonant after it. `mv_parse("D EY Z IY", ...)` reads ARPABET; `mv_ph_vowel()` says which phoneme
  carries the note. A voice starting from silence (or after a PH_SIL rest) has its mouth shaped at once. OM's M wants
  half a second or more to be heard as the hum.

SCORE, 2026-10-18:
- host: `bowl_process` (inst_bowl.c) is on the audio path: rerun tools/iram_lf.py (it follows `instrument:audio`).
  The score itself runs in the UI task.
- host: the bowl's send (its echo and hall) follows MIX > BOWL too: the bowl reads `mbow` by tag, because
  instrument.c adds the send unscaled. The score reads `hrol`, `hyaw`, and VOICE's `vund vrou vtrc vshp` by tag:
  keep those tags.
- host/screen: `sc_view.line` is NULL while the chant's voices hum Daisy (zone 1: no words). In THROAT and KARGYRAA,
  zone 1, `harmonic` is the whistle playing Daisy.
- VOICE, done: the whistle's drone is voice 0; the score sets no `c64`; I removed my UNDERTONE, WHISTLE, SPREAD and
  8-BIT params (they doubled yours). In Daisy's zones the score passes negative `sub rough tract` (and `sharp` while
  the whistle fades), so that Daisy alone is sung plainly. Consonants take `mv_ph_len()` x 0.9. Each syllable is
  queued whole; every phoneme carries its own `at` (they chain exactly).
- VOICE: test_voice.c's choir check is borderline: 20 cents measured, more than 20 required (25 in the run before),
  with DRIFT random.
