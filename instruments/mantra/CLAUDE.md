# CLAUDE.md

Guidance for Claude Code in this folder.

## What this is

MANTRA: a formant voice synth (a little an effect) on the library's shared core (from WRECK's copy, with the
SPEAKER switch and the stuck-REC guard). A seated Buddha sings: monks' OM, OM MANI PADME HUM, overtone singing
(KHOOMEI, SYGYT, KARGYRAA), the chorus of "Daisy Bell" (Harry Dacre, 1892, public domain) as the IBM 7094 sang it in
1961, a free vowel player (the Delay Lama's way: AudioNerdz' singing-monk plugin) and HUM ALONG (the voice follows
the mic). Built 2026-10-17 by a lead and two builders while the user was away (MANTRA_NOTES.md: the plan, the rules,
the sounds, Daisy Bell's chorus as data); first flashed 2026-10-18 (board 727600D0). README.md is for players, docs/MANUAL.md explains
everything (its tables are generated: `python3 tools/manual_tables.py`), docs/CORE.md is the core.

SAM (Software Automatic Mouth) is not used and not imitated: its circulating code is a reverse-engineered copy of a
commercial program. The 8-bit voice is the engine's own mode.

## Files

- `firmware/main/mantra.h` — the contract: `song_t` and its beat helpers, `hands_t`, `module_t`, SING's FX bus API.
- `instrument.c` — the host: the param table (24 slots per module: VOICE 0, SCORE 24, then the host's), the sounds
  (the host's part joined with each module's), the song and the link (as SING's), the hands (tilt = jaw, roll,
  yaw from gyro z, pumping from the acceleration's swing, the stick, knocks = the bowl), the voices' controls
  (`mv_ctl_t`, then `sc_tick` sets the voices, then `mv_set`), the mix, the FX bus, REC = the core's looper.
- `inst_voice.c` / `inst_voice.h` (VOICE), `inst_score.c`, `inst_bowl.c` / `inst_score.h` (SCORE), each with its test
  (`tools/sim/test_voice.c`, `test_score.c`).
- `inst_scene.c` — the screen: the Buddha (a silhouette with a dithered robe, the face's features, the mouth from
  `mv_view`), the halo and sound rings, the monks, the harmonic ladder, the words, the bowl; the splash, the saver.
- `inst_drone.c` / `inst_drone.h` (DRONE, 2026-10-18): WRECK's AMP at its doom end, ported (one chord held from the
  song's chords on bar lines per CHANGE, swells, feedback, sub, hum, organ, wah/tremolo); `tools/sim/test_drone.c`.
  The host passes `dr_ctl_t` (breath, bend, the stick's dive, knocks, MIX > DRONE) and the scene draws the wall of
  amps from `dr_view`. `host_degree_at(frame)` (the host's) gives it the chord exact on a bar line.
- `inst_fx.c` — SING's input stage and FX bus, unchanged but for its include.

## Rules

- The audio rules of docs/CORE.md. Budgets per 64-frame block on the host: VOICE 8 us with six voices singing,
  SCORE + bowl 1, DRONE 5, all of MANTRA 20.
- After changing what the sound calls: build, `python3 tools/iram_lf.py firmware/build firmware/main/mantra_iram.lf
  mantra_audio instrument:audio front:front_audio` (the first time; later just the .lf), build again.
- Song params are PF_SYS and shared on the link: a sound cannot set the tempo.

## Measured (tools/sim, 2026-10-18 after the sweep and the drone; the board's numbers at the end)

160 checks. The busiest sound (SUNN): 13.7 us per 64-frame block (budget 20); six voices saying words 6.4, six silent
0.15, the score 0.08, the bowl ringing 0.37 (silent 0.003), the drone with everything on 4.05 (asleep 0.09), the FX
bus 3.8 at work and 0.04 asleep, the mic effect about 2 more. Device image 826 KB, IRAM list 24 functions (78 KB).
Soaks (opt-in): `MANTRA_SOAK=<s a mode> tools/sim/run.sh inst` (an hour a mode ran clean). Whisper (the local whisper.cpp) as an ear: 30 of the chorus' 40 words at about
84 BPM dry, 17 at the default 72 with the waltz; N and NG at a word's end can come out as M.

On the board (727600D0, OM, before the sweep and the drone): cpu 33-37 % (max 47 %); per 2000 us block the voices
125-200 us, the bowl 60 (it never stopped: fixed), the FX bus 288 (now asleep when idle); internal heap 50 KB free.
The drone and the heavier sounds not measured on a board yet; the board now runs the Samplooler.

## Commands

```sh
. ~/esp/esp-idf/export.sh && cd firmware && idf.py build      # firmware
tools/sim/run.sh                                               # every check; out/: screens, GIFs, WAV
python3 tools/manual_tables.py                                 # the manual's settings tables
tools/flash.sh                                                 # flash (only when the user asks)
```
