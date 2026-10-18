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
| tap the knob | open the menu | open a page, edit a setting, run an action; tap again to go back up |
| hold the knob 0.6 s | hold the motion (freeze): tilt and spin stay where they are, a padlock shows; again to let go (or the instrument's own use of it: `INST.hold`) | back to play |
| REC (or BOOT) | the instrument's; with `INST.looper` the core's looper: hold = record (or add), tap = stop / play. SYSTEM > REC BUTTON OFF ignores the REC pin (BOOT and MIDI CC 64 still record); `INST.rec_button_off` starts it OFF | the same |
| joystick | the instrument's (when one is fitted) | |
| REC held 3 s from power-on, then let go | factory reset (tilt setup kept); a REC still down after 6 s is taken as stuck: no reset, REC ignored until it lets go, BOOT records meanwhile. Not with REC BUTTON OFF (RESET ALL in the menu still works) | |
| knob held at power-on | USB stays the Serial/JTAG console (for `idf.py monitor`) | |

The menu's pages: SOUND (choose, SAVE AS a user sound, REVERT), the instrument's pages (`INST.groups`),
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
after the factory ones. Program change n picks sound n (factory first, then user).

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

## The coach

`INST.hints`: what to try, each tied to a LEARN bit. The core shows one at the bottom when the player has
been idle a few seconds, and drops each for good once the player has done it (the core marks turning,
the menu, REC, holding, the joystick and tilting; mark your own with `coach_done(LEARN_INST + k)`).
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
SysEx `F0 7D 52 F7` reboots into download mode, `F0 7D 53 F7` logs status and the last crash.

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
`link_set_flags(LINKF_LOOP)`. It adds `link.c link_esp.c` to its sources and `esp_wifi esp_event` to its
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
instrument's own scenarios.

## Flashing

`tools/flash.sh` finds the board by name over USB MIDI (a board running any of these firmwares or the
Samplooler), tells it to reboot into download mode, and flashes the serial port whose MAC matches its
USB serial. It never writes to anything else, a TWOFOLD included. `BOARD=<serial>` picks one of several.
