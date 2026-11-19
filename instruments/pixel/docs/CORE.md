# The core

Every instrument in the library shares one core, copied into each project (they are separate firmwares,
each in its own folder, so each folder builds and ships on its own). This is how the core works and what an
instrument has to provide. The instrument itself is `firmware/main/instrument.c` (plus any `inst_*.c`).

## Hardware

The Samplooler's S3 boards: the Stack, the Mini and the breadboard kit (ESP32-S3 SuperMini, 4 MB flash,
2 MB PSRAM). `pins.h` is the Samplooler's map. INMP441 mic, PCM5102A line out, optional MAX98357A speaker
amp, MPU-6050 (accelerometer + gyro), 128x64 SSD1306 on the same I2C bus, encoder with push, REC button,
joystick (Stack and kit; found at boot by a pull-up/pull-down test on its pins, so the Mini runs without
it), WS2812 RGB LED on GPIO 48, native USB (MIDI + a 32 kHz sound card).

Rev E (the Vessel) has no joystick. On the same two pins it has a second knob, the left one, whose push is REC, and
in the middle of the board a VL53L1X that measures how high a hand is held over it (`tof.c`, 0x29 on the I2C bus).
The core finds both by itself: with no joystick the two pins are counted as a knob, and the board is taken for a
rev E when the sensor or the GY-87's barometer (0x77) answers (SYSTEM > LEFT KNOB ON takes the knob as fitted on a
rev E that shows neither; its counts alone are not trusted, since a rev C whose stick failed its test would count too). The same firmware
runs on rev C and rev E. None of this has run on a board yet: no rev E has been built.

## Files

| File | What | Host (tools/sim) |
| - | - | - |
| `core.h` | everything an instrument sees: `ctl_t`, `param_t`, `preset_t`, `hint_t`, `view_t`, `instrument_t`, the platform functions | yes |
| `front.c/h` | the front panel: controls to menu / sounds / instrument, the frame, the audio chain, MIDI in/out | yes |
| `params.c/h` | the param table at run time: format, step, clamp, CC, presets, flash packing | yes |
| `looper.c/h` | the core looper (instruments with `INST.looper`) | yes |
| `ui.c/h` | the drawing kit: header band, meters, pills, sprites, particles, easing | yes |
| `calib.c/h` | tilt setup's arithmetic | yes |
| `dsp.h` | DSP pieces and the master limiter | yes |
| `oled.c/h`, `font*.{c,h}` | the panel, TWOFOLD's proportional fonts | yes |
| `link.c/h` | the flock: boards that share a clock, a tempo, a loop's length and a harmony over ESP-NOW (instruments that call it) | yes (`tools/sim/link_sim.c`, `tools/linktest`) |
| `link_esp.c` | the link's radio (ESP-NOW on Wi-Fi channel 1) | no |
| `shell.c` | the device: boot, drivers, the 10 ms loop | no |
| `audio.c/h` | I2S full duplex, 64 frames at 32 kHz, float, core 1; mute ramp; health counters | no |
| `imu.c/h`, `input.c/h`, `led.c/h`, `usb.c/h`, `usb/`, `diag.c/h`, `settings.c/h` | drivers, USB MIDI + sound card, crash notes, NVS | no |
| `components/esp_driver_i2c/` | ESP-IDF's I2C driver with one fix (a bounded wait after a NACK; see its README) | no |

## Controls (the same on every instrument)

| | In play | In the menu |
| - | - | - |
| turn | change the sound (or the setting named in `INST.knob`); once a setting has been edited in the menu, the knob turns that setting instead (a strip shows it) | choose, or change the value being edited |
| tap the knob | open the menu, 0.3 s later (`DOUBLE_MS`: a second tap may still come) | open a page, edit a setting, run an action; tap again to go back up |
| tap it twice | the knob turns SYSTEM > VOLUME (a strip shows it) until it rests 3 s (`VOL_MS`) or is tapped; not while the instrument holds the knob (`knob_grab`). The volume glides in `master_process` (about 50 ms), so the steps don't zipper | |
| hold the knob 0.6 s | hold the motion (freeze): tilt and spin stay where they are, a padlock shows; again to let go (or the instrument's own use of it: `INST.hold`) | back to play |
| REC (or BOOT) | the instrument's; with `INST.looper` the core's looper: hold = record (or add), tap = stop / play. SYSTEM > REC BUTTON OFF ignores the REC pin (BOOT and MIDI CC 64 still record); `INST.rec_button_off` starts it OFF | the same |
| joystick | the instrument's (when one is fitted) | |
| left knob (rev E) | the instrument's: `ctl.lknob`, detents a tick, when `ctl.has_lknob` | the same |
| a hand over the sensor (rev E) | the instrument's: `ctl.hand`, 0 no hand to 1 fully down (25 cm to 4 cm), gliding, 0 again 0.25 s after the hand has gone; `ctl.hand_seen`, `ctl.hand_mm`, when `ctl.has_hand`. SYSTEM > HAND OFF ignores the sensor. Sent as CC 16 with MIDI CC OUT | the same |
| REC held 3 s from power-on, then let go | factory reset (tilt setup kept); a REC still down after 6 s is taken as stuck: no reset, REC ignored until it lets go, BOOT records meanwhile. Not with REC BUTTON OFF (RESET ALL in the menu still works) | |
| knob held at power-on | USB stays the Serial/JTAG console (for `idf.py monitor`) | |

The menu is a strip over the bottom 18 rows of the scene, which goes on playing (as the Samplooler's own menu strip;
the full-screen list went on 2026-10-22): the pages side by side as pills, then a page's settings one at a time with
the page's name small above, then the setting being edited with its slider. The menu's pages: SOUND (choose, SAVE AS a user sound, REVERT), the instrument's pages (`INST.groups`),
LOOP (with the looper), SYSTEM (volume, speaker on/off, mic gain, tilt setup, motion on/off, joystick auto/off and its
orientation, the REC button on/off, MIDI channel, MIDI CC out, LED, hints, screen saver, ABOUT, RESET ALL).

## An editor of the instrument's own

`INST.hold` is called when the knob is held 0.6 s in play; returning true takes that gesture (BAND opens its drum
grid with it) and the motion is not held. `knob_grab(true)` then gives the instrument the knob in play: the core
neither changes the sound nor opens the menu, hints stop, and every tick `ctl.knob` (detents), `ctl.knob_tap`,
`ctl.knob_hold`, `ctl.knob_down` and `ctl.knob_turned` (pressed and turned: a drag) say what it did.
`knob_grab(false)` gives it back.

## Sounds

`INST.presets` are the factory sounds: a name and `"tag=value ..."`; everything not named is the param's
default. Turning the knob in play walks the list: an edited sound that is left is kept as MY EDIT at the
start of the list; SAVE AS stores up to eight user sounds (USER 1..8, named after the sound they came from)
after the factory ones. Program change n picks sound n (factory first, then user). An instrument whose
sounds are something else can name them: `INST.sound_word = "SONG"` (PIXEL) changes the menu's words, nothing more.

## Params

One table per instrument (`param_t`, indexed by your own enum). Values live in `pv[]` as floats: read them
anywhere, the audio task included; change them only through `param_set()` (UI task). `changed(p)` tells the
instrument (p = -1 after a sound loads). Each param is on MIDI CC 20 + index (CC 20..63, then 70..119),
both ways. Flash keeps them by tag, so adding or removing params never breaks what is stored.

Write param names in caps (<= 70 px in F_SMALL), help lines that say what the param does in a few words
(<= 124 px in F_TINY), units that make the value readable (`"s"` prints ms below 1 s, `"Hz"` prints kHz
above 1000). `PF_LOG` for frequencies and times. `PF_SYS` for device-like settings that should not change
with the sound. The sim checks every one of these limits.

## The audio rules (the Samplooler's and TWOFOLD's)

- `audio()` runs every 2 ms on core 1. No allocation, logging, locks, `double`, or libm per sample
  (block-rate coefficients are fine). `-O3 -ffast-math -Wdouble-promotion -Werror`.
- Every control that reaches the sound is smoothed. The sim jumps every param from end to end while the
  instrument sounds and fails it on a click (a first-difference spike > 3x the signal's own).
- Voices are stolen with a short fade, never cut. Structural switches (a new model, a new kit) crossfade or
  duck for a few ms.
- Nominal peaks around -12 dBFS. After the instrument (and the looper) comes the master: volume, a 10 Hz
  DC blocker, a peak limiter at -1 dBFS (instant attack, 80 ms release) and a soft knee: a safety net, not
  part of the sound. Nothing reaches the DAC over full scale.
- Decaying recursive states are flushed (`flush_denormal`) so silence costs nothing.
- The mic arrives high-passed at 80 Hz with the MIC GAIN applied. What the speaker plays comes back into
  the mic: anything that listens (breath, knocks, voice) discounts its own output. SYSTEM > SPEAKER: OFF holds
  the speaker amp in shutdown (its SD pin low) while the line out and USB play on: the cure for feedback.
- Talk to the UI side with plain 32-bit variables and `spsc.h` queues; never wait.
- The audio code runs from IRAM: each instrument lists the object files of its audio path in a linker
  fragment (`firmware/main/<name>_iram.lf`, `(noflash)` entries; `front_audio()` is `IRAM_ATTR`). Measured on
  the board (JAM, 2026-10-14): the same blocks from flash, through the cache that shares the SPI bus with
  PSRAM, cost 130 % of the CPU and reset the board through the watchdog; from IRAM, 48 %. Code called once a
  block can stay in flash; anything called per sample must not.
- CPU on the board is roughly 70x the host's time in `tools/sim` (JAM: 14 us on the host, 0.8 to 1.1 ms of the
  2 ms block on the S3, with the audio in IRAM). Keep the host's busiest case under about 15 us, and confirm on
  the board: SYSTEM > ABOUT and the status line (`python3 tools/sl_midi.py status`) show the CPU and the drops.

## The screen rules

- Rows 0..15 are the header band (yellow on two-colour panels): `ui_head()` there. Rows 16..63 are the scene.
- The core draws over the bottom 18 rows when it needs to (an edit strip, the sound card, a hint, a boot
  note): `view_t.cover` says so; keep only things the player can do without for a moment there.
- 25 fps. Animate with time (`view_t.t`) or a frame count, ease (`ease_out`, `ease_back`), use particles.
  The scene shows what the sound is doing, so the screen teaches the instrument.
- A splash at boot (`INST.splash`, up to 4 s, skipped by any control) and, if you like, a screensaver.
- Copy is short, plain and in caps on the screen.

Motion at 1 kHz: `INST.burst` fills `ctl.burst` (the accelerometer samples since the last tick, oldest first, screen axes);
`INST.burst_gyro` adds `ctl.gburst`, the gyro at the same instants (°/s; NULL while frozen). The chip's FIFO then holds
85 ms, so it is read every tick. The simulator's `SI.burst` and `SI.gburst` feed them. The motion stream records them
from a board, and the simulator plays them back ("Motion takes" below).

## The coach

`INST.hints`: what to try, each tied to a LEARN bit. The core shows one at the bottom when the player has
been idle a few seconds, and drops each for good once the player has done it (the core marks turning,
the menu, REC, holding, the joystick, tilting, the left knob (`LEARN_LKNOB`) and the hand (`LEARN_HAND`); mark your
own with `coach_done(LEARN_INST + k)`). A `LEARN_JOY` hint shows only on a board with a joystick, a `LEARN_LKNOB` or
`LEARN_HAND` hint only on one with that part: write a hint for each, and each board shows its own.
SYSTEM > HINTS brings them back. Two lines of 112 px in F_SMALL at most. An instrument with a full-screen
flow of its own (a setup run from the menu) calls `menu_leave()` to close the menu and `coach_pause()` each
tick while the flow is up.

## MIDI

Out: params as CC 20+ when changed on the device, program change on a sound change, CC 12/13 the lean
(y, x), CC 14/15 the joystick (x, y) when SYSTEM > MIDI CC OUT is on, plus whatever the instrument sends
(notes, its own CCs, pitch bend with `midi_bend()`; keep CC 1..11 for that). In: CC 20+ sets params,
program change picks a sound, CC 64 (a sustain pedal) is REC, note 60 is REC and note 64 toggles the motion
hold unless the instrument plays MIDI notes (`INST.midi_notes`: then every note goes to it), and the rest
goes to `INST.midi_in`. One channel (SYSTEM > MIDI
CH) for everything except what the instrument sends on purpose elsewhere (`midi_note_ch`, drums on 10).
SysEx `F0 7D 52 F7` reboots into download mode, `F0 7D 53 F7` logs status and the last crash, `F0 7D 4D 01 F7` and
`F0 7D 4D 00 F7` start and stop the motion stream ("Motion takes" below).

## The app's line

The desktop app (`app/` in the repository) shows a board's settings and changes them over the same MIDI port
(`front_sysex()` in `front.c`, protocol 1). Each message is SysEx `F0 7D <letter> <text> F7`, the text 7-bit
ASCII, one `key=value` per line:

| | |
| - | - |
| in `I` | who are you: answered with `I` `name= version= serial= proto=1 params=<count>` |
| in `D` | describe the settings: one `D` per setting (`i tag name group kind min max step unit value text flags cc labels help`), two per tick, then `D` `end=<count>` |
| in `P` `tag=value` | set one by its tag; an action (`kind=act`) runs at 1 or more; answered with `V` |
| out `V` | `tag= value= text=`: a setting changed on the board (the knob, MIDI, a sound) |

Every param is described except RESET ALL, ABOUT and TILT SETUP, which need the board's screen, and the LOOP
page without `INST.looper`. Changes go out only once the app has the whole list and while it keeps talking:
the app says `I` every 2 s while it shows the board. After 5 s without a word from it, or a message that could
not go out, the board stops sending; the next word from the app brings the changes it missed. An instrument
needs nothing for this: its params table is the description. The checks in `tools/sim/core_checks.c`
("app line") cover it. The Samplooler speaks the same protocol over its own settings (`firmware/main/app_line.c`).

## The link (a flock of boards)

`link.h` lets boards play together: each one that calls it broadcasts a small packet every 100 ms over ESP-NOW
(Espressif's own protocol on the Wi-Fi radio: no router, no pairing). The board with the highest priority (then the
lowest MAC) is the clock; every other board measures its offset to it from the packets' send and receive times, as NTP
does (the round trips least delayed win, a PI servo follows the crystals' drift), and maps its own audio frames to the
clock's. The session they share: the tempo as a whole number of frames per beat, the frame of beat 0, the session's
loop length, and a harmony (key, scale, progression, a chord played live). It is leaderless like Ableton Link: any
board may change it and the newest change wins; a board meeting a clock board joins its session; a session with a loop
outranks one without, so a board switched on late never moves a loop that plays. Measured with `tools/linktest`
(several pretend boards, crystals 35 ppm apart, a lossy air): the beats agree within 0.5 ms (1 ms with a quarter of
the packets lost and 8 ms queues).

An instrument calls `link_init(name, priority)`, `link_boot_time()` and `link_boot_harm()` once, `link_enable()` from
its settings, `link_tick()` every tick, and reads `link_origin()` / `link_fpb()` (beat 0 in its own frames and the
frames per beat) and `link_harm()`; it sets the session with `link_set_time()`, `link_set_harm()` and
`link_set_flags(LINKF_LOOP)`. An instrument can also send a few bytes of its own state with every packet
(`link_set_app()`, up to `LINK_APP_MAX`, 16; `link_poke()` sends at the next tick) and read each peer's latest in
`link_peer_t.app` (DWELL shares its root, its energy and its lit cells this way). They ride after the clock's echoes,
where a board on an older core does not look, so mixed flocks keep working. It adds `link.c link_esp.c` to its sources and `esp_wifi esp_event` to its
`INSTRUMENT_REQUIRES`, and the simulator builds `link` and `link_sim` (a pretend room: `sim_peer_add()` and friends in
`sim.h`). Packets go out at low power (2, 8 or 14 dBm: every one is a burst of current from the DAC's supply).

## Robustness (from the Samplooler's production pass)

Crash notes on screen and in the log after a panic, a brownout or a watchdog; safe mode (defaults) after
three crashes in a row; the task watchdog on the audio task and the control loop; the speaker amp woken
after 50 ms of clean I2S; settings saved 3 s after a change in a quiet moment (never mid-phrase if it can
help it); a screen or IMU that stops answering is backed off and retried; the I2C bus is cleared at boot
and reset after a run of failed reads.

## The simulator

`tools/sim/run.sh` builds `front.c`, `params.c`, `looper.c`, `ui.c`, `calib.c`, `oled.c`, the fonts (and `link.c`
with `link_sim.c` for instruments on the link) and the instrument on the host with stand-ins for the drivers, and runs the core's checks (tables, sounds,
flash and MIDI round trips, the whole menu, tilt setup for all 24 mountings, the looper, every param's
edges for clicks, the screensaver, the factory reset, no NaN, nothing over full scale), then the
instrument's own (`tools/sim/test_inst.c`). Out: `out/<name>.png` (every screen shot), `out/<name>-*.gif`
(animations at 25 fps), `out/<name>.wav` (all of it), `out/results.json`. `./run.sh inst` runs only the
instrument's own scenarios. With `SIM_WAV_F32` set the WAVs are 32-bit float, NaN and overs kept: `tools/sounds.py`
in the Samplooler's repo reads each instrument's `<id>-rev-e.wav` that way, for the sound of every commit. `tools/sim/take.c` replays recorded motion (below): each instrument's `run.sh`
compiles it with the sim's own files (`sim core_checks main take ...`).

The sim boots as a rev C with a joystick. `sim_rev_e()` turns it into a rev E from then on (no joystick, the left
knob and the hand's sensor); `sim_lturn(n)` turns the left knob n detents on one tick; `sim_hand(mm, seconds)` holds
a hand at that height for that long (0 mm: taken away). `front_ctl()` is what the instrument was handed last. Test
what the instrument does with both on a rev E, and that nothing changed on the board with the joystick.

## Motion takes

A player's real gestures, recorded from a board and replayed in the simulator as tests.

**The stream** (`front.c`). SysEx `F0 7D 4D 01 F7` ('M' 1) starts it, `F0 7D 4D 00 F7` stops it; it is off at boot.
While it is on, an instrument with `INST.burst` sends one message a tick with what the instrument was just given
(`ctl.burst`, `ctl.gburst`, screen axes):

| Bytes | |
| - | - |
| `F0 7D 6D` | 'm' |
| 1 | a sequence number, 7 bits, wrapping |
| 5 | `ctl.frames`, 7 bits each, most significant first |
| 1 | flags: 1 REC down, 2 the knob down, 4 gyro present (none on a tick without samples, or while the motion is held) |
| 1 | n, the tick's samples: about 10, at most 20 on the board |
| 12 n | each sample's ax ay az gx gy gz, a signed 14-bit value each as two 7-bit bytes, high first: accel in 1/1024 g (clamped at 8 g), gyro in 1/4 deg/s (clamped at 2000) |
| `F7` | |

That is 12 + 12 n bytes: 132 for ten samples, 13.2 KB/s of SysEx. On USB it travels as 44 four-byte USB-MIDI packets a
tick (17.6 KB/s), three 64-byte bulk transfers. TinyUSB starts the next transfer as soon as one ends; even one a
1 ms USB frame would carry 64 KB/s, so the stream needs about a quarter of the slowest case, beside the sound card's
isochronous streams. TinyUSB's MIDI buffer holds 1024 bytes, about 58 ms of the stream. Computed, not yet measured on
a board: the first takes will show it (the sequence numbers and the board's drop count).

The message goes out from the UI task and never waits (`plat_sysex_try` in `usb.c`): the MIDI lock is tried once (a
log line from another task may hold it), and a message that does not fit in TinyUSB's buffer is dropped and counted.
After 2 s in which nothing went out (nobody reads) the stream stops by itself. The log says `motion: on` and
`motion: off, N sent, M dropped`. The stream carries the 1 kHz samples only: `ctl.acc` and `ctl.gyro` (the 100 Hz
reading) come back in the simulator as each tick's last sample.

**The recorder** (`tools/motion_rec.py`, Python, no packages; it finds boards by their MIDI port's name, never a
TWOFOLD, and reads each in a thread of its own):

    python3 tools/motion_rec.py list
    python3 tools/motion_rec.py take left-x10 --secs 20 --label '{"hits": {"LEFT": 10}, "others": 0}'
    python3 tools/motion_rec.py guided --instrument airdweller [--all] [--from drag]
    python3 tools/motion_rec.py show ../../lab/takes/airdweller/*.take

`take` starts the stream, counts down 3-2-1, records, stops the stream and saves. `guided` walks through
`instruments/<id>/tools/takes.json`, a list of `{name, prompt, secs, label}`: Enter records, r redoes, s skips, q
stops. `--all` records every board at once, each to its own file, with the same name and the same start. `stop`
stops a stream left running; `convert` turns raw SysEx bytes (`amidi -r`) into a take.

Takes go to `lab/takes/<instrument>/<YYYY-MM-DD>-<name>-<serial>.take` beside the repository (`MOTION_TAKES=<dir>`
elsewhere). A take is a JSON header line (instrument, version, serial, date, take, prompt, label, rate, axes, units,
ticks, gaps, the board's sent and dropped counts), then one line per sample: `frame ax ay az gx gy gz flags`. Frame is
in audio frames from the first sample, accel in g, gyro in deg/s; the flags as above, plus 8 on the first sample
after messages were lost. A tick's last sample sits at the tick's frame count, the others spread evenly back to the
tick before (32 frames apart after a gap).

A label says what the replay should find: `{"hits": {"LEFT": 10}, "others": 0}` exactly those hits and no others;
`{"only": "LEFT"}` any number, all LEFT; `{"hits": {}}` none; `{"any": true}` anything, but no NaN and nothing stuck;
`{}` nothing to check. An instrument may add keys of its own (AIRDWELLER's `drags`).

**The replay** (`tools/sim/take.c`, `take.h`). `take_load()` reads a take whole. `take_begin()` then `take_feed()`
before each `sim_tick()` puts each sample in the tick its frame falls in: `SI.burst`, `SI.gburst`, `SI.nburst`;
`SI.acc` and `SI.gyro` from the tick's last sample; `SI.rec` and `SI.pressed` from the flags (a press of 40 to 500 ms
also taps, as on the board). A gap is ticks without samples: time runs on, nothing is made up. `take_run()` plays a
take to the end; `take_num(t, "label.hits.LEFT", -1)` and `take_keys(t, "label.hits", ...)` read the label. `lab/` is
not in the repository, so a test that replays a take should skip it when the file is not there.

The core's check ("motion stream and takes") runs for every instrument, after the instrument's own tests (so the time
and the motion it takes cannot change what they hear): one message by hand (every field, the clamps),
a synthetic take with a gap through the feeder, the instrument's `ctl` and the stream (back within half a 14-bit step,
every sample in its tick), REC and the knob from the flags, and `motion_rec.py verify` on the same bytes (packed again
byte for byte, the same take, three lost messages, the counters wrapping).

## Flashing

`tools/flash.sh` finds the board by name over USB MIDI (a board running any of these firmwares or the
Samplooler), tells it to reboot into download mode, and flashes the serial port whose MAC matches its
USB serial. It never writes to anything else, a TWOFOLD included. `BOARD=<serial>` picks one of several.
