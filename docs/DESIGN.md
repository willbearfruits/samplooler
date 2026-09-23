# Tilt recorder: design note

Written 2026-09-20. The design this project implements. The ESP32-S3 SuperMini kit repo (~/Projects/kit) is the reference for wiring and drivers; this is a standalone ESP-IDF project.
Hold BOOT to record the microphone, let go and it plays back in a loop. Tilting
the box sets the playback direction and speed. The first version is a toy that
works; the musical elaborations are listed at the end so they can be picked one
at a time in later sessions.

## Hardware

Same parts and pins as the kit, so a built kit runs this without rewiring:

| Part | Role | Pins |
| - | - | - |
| ESP32-S3 SuperMini | brain, 2 MB PSRAM | |
| INMP441 (round 6-pin capsule) | mic in | I2S RX, GPIO 7/8/10 |
| PCM5102A | line out | I2S TX, GPIO 7/8/9 |
| MPU6050 (ITG-style gyro + accel) | tilt | I2C, GPIO 12/13, address 0x68 |
| BOOT button | record | GPIO 0 |

The OLED, encoder and joystick are optional for this app. It must run with only
the four parts above wired. The IMU driver (`imu.c`) already gives filtered
roll and pitch in degrees; the input driver already reports BOOT as a press.

Gotcha to keep from the other apps: BOOT held during a reset lands in
download mode, and BOOT held at power-on is the USB drive mode check. Recording
is a momentary hold after boot, so neither is a problem in normal use.

## Behaviour, version one

- **Idle.** Silence out. Screen (if present): "hold BOOT to record".
- **Record.** While BOOT is held, the mic is written into a PSRAM buffer from
  the start. Releasing ends the take; the loop length is what was recorded.
  Holding past the buffer limit stops recording at the limit. A new hold
  replaces the take. Pass the mic through to the output while recording so
  the player can hear what is going in.
- **Play.** After release the loop plays continuously. Loop ends are joined
  with a short equal-power crossfade so the seam does not click.
- **Tilt.** The pitch axis (nose up, nose down) controls playback:

  | Tilt | Rate |
  | - | - |
  | flat, within a small dead zone | very slow forward, about 0.05x, so the sound never fully freezes |
  | nose down, increasing | forward, 0.05x rising to 2x at 45 degrees |
  | nose up, increasing | backward, 0.05x rising to 2x at 45 degrees |

  Rate follows the angle continuously with a curve that is gentle near flat
  and steep near the ends, so small movements are audible without being
  twitchy. The roll axis is left free for the musical version.

- **No IMU present.** Play forward at 1x. The app must not crash or stall
  without the sensor, matching how the looper treats it.

## Numbers

| Item | Value | Why |
| - | - | - |
| Sample rate | 32 kHz (existing engine setting) | shared by mic and DAC |
| Block | 64 frames (existing) | about 6 ms round trip |
| Buffer format | 16-bit signed, mono | halves PSRAM use versus float, plenty for a mic |
| Buffer size | 20 s = 640 000 samples = 1.25 MB in PSRAM | leaves room for the rest of PSRAM users |
| Rate range | -2x to +2x | beyond 2x the INMP441 recording aliases audibly |
| Rate slew | about 50 ms | no zipper noise when the box moves fast |
| Loop crossfade | 10 ms | seam free |
| IMU poll | 50 Hz | existing filter tuned for that |
| Input poll | 100 Hz | existing |

## DSP core (hand written C, about 150 lines)

Playback is a fractional read head `pos` moving by `rate` samples per output
sample, wrapping in both directions over the loop length. Read with 4-point
cubic (Hermite) interpolation from the 16-bit buffer; that is what makes slow
backward playback smooth instead of gritty. Near the loop boundary read the
sample from the other end as well and crossfade over the 10 ms window.

`rate` is set from the UI task as a target, and the audio task slews toward it
per sample. The record head is a plain integer write index. State lives in one
struct owned by the audio task; the UI task talks to it through a few atomics
(target rate, record flag, and back the other way the loop length and the head
position for the display).

This is deliberately C, not Faust. Faust's tables are fixed-size class members
and a 640 000 sample table would need custom PSRAM placement and awkward
bidirectional reading. The Faust path (already wired via `tools/faustgen.sh`)
is the right tool for the sound shaping added later, sitting after the reader.

## Fit into the firmware

- New Kconfig choice `KIT_APP_TILT` under the existing `KIT_APP` list, and
  `tools/app.sh tilt`.
- `fx_tilt.c` implements an `fx_t` (`init`, `process`, `status`); `fx_list.c`
  picks it for this app, so the engine changes are zero.
- `app_tilt.c` is the loop: `input_poll` at 100 Hz, `imu_poll` at 50 Hz, map
  tilt to rate, push it to the fx, draw the screen. Modelled on `app_test.c`,
  which is the smallest app.
- Buffer allocated once with `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`, the
  same way the looper allocates its vocal slots.
- Screen: state word, a bar for the loop with the head position, the rate as
  a signed number, a small tilt indicator.
- Serial log: state changes and the loop length in seconds, so it can be
  checked without the screen.

## How to verify on the board

1. Flash, speak while holding BOOT, release. Hear it loop forward at slow speed
   when the box is flat.
2. Tilt nose down: it speeds up. Tilt nose up: it reverses and speeds up.
3. Move the box quickly: no zipper noise, no clicks at the seam.
4. Unplug the IMU and reboot: forward 1x, no crash.
5. Hold BOOT for 30 s: recording stops at 20 s, playback works.
6. CPU meter on the serial log stays well under half a core at 2x.

## Making it musical (later, one at a time)

- **Speed snapping.** Quantize rate to musical ratios (2, 1, 1/2, 1/4 and their
  negatives, or semitones) with a small hysteresis so tilting steps through
  intervals instead of sliding. A toggle between smooth and snapped.
- **Roll axis.** Left-right tilt to a low-pass cutoff, or to the loop window:
  playing only a slice of the take that moves with the tilt.
- **Pitch independent of speed.** A Faust pitch shifter after the reader
  (already in the showcase as `shift.dsp`) so a slowed loop can stay in tune,
  or a fast one can drop an octave.
- **Overdub.** Tap BOOT (short press) to layer a new take on top, hold to
  replace. Needs a feedback level to keep layers from clipping.
- **Grains.** Instead of one read head, several short windows scattered around
  the head position. Tilt magnitude sets the spread. This is where the toy
  becomes a texture instrument.
- **Tempo lock.** Tap tempo on the joystick click, rate snapped so the loop
  length lands on a bar.
- **Envelope from motion.** Sudden shakes (gyro rate, not tilt angle) trigger
  a stutter or retrigger from the loop start.
- **Save takes.** Write the buffer as a WAV to `/storage` and expose it over
  the existing USB drive mode.

Pick the first two before any of the others; they are cheap and change the
feel the most.

## Note: line input for a synth or guitar (2026-09-21, not built)

The recorder only needs a different input stage. Add a **PCM1808** I2S ADC module
(stereo line in, 24-bit) on the S3's **second I2S port**, so the mic stays and
"Input: mic / line" becomes a menu setting.

| PCM1808 | S3 (kit) | Notes |
| - | - | - |
| VCC | 5V | the module regulates its own 3.3 V |
| GND | GND | |
| SCK | GPIO 21 (spare) | master clock, 256 x fs = 8.192 MHz at 32 kHz, output by the I2S peripheral |
| BCK | new pin | I2S1 bit clock |
| LRC | new pin | I2S1 word select |
| OUT | new pin | I2S1 data in |
| FMT | GND | I2S format |
| MD1, MD0 | GND, GND | slave, 256 fs |

Firmware: open I2S1 as RX only with `mclk` set, 32-bit slots, same block size; read it
in the audio task next to the mic; pick the source per the setting; gain default 0 dB
for line; no slot detection (a line ADC drives both slots). Keep stereo as an option:
the take buffer would double.

Guitar: a passive pickup into the PCM1808's ~20 kΩ input sounds dull. Put a buffered
pedal, a DI box or a preamp in between; then it is line level like the synth. Active
pickups do not need it. Do not use the ESP32's own ADC for this (12-bit, noisy).
