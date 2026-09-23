# Gesture recorder

ESP32-S3 SuperMini or ESP32-C3 SuperMini, INMP441 capsule mic, PCM5102A DAC, MPU6050
tilt sensor, and the BOOT button. The C3 build drives a 0.42" OLED. Hold BOOT and talk: the take goes into PSRAM. Let go and it loops.
Tilt the box nose down and it plays forward, faster the further you tilt. Tilt nose
up and it plays backwards. Flat is a slow forward crawl so the sound never freezes.

## Where things are

| Path | What |
| - | - |
| `main/` | firmware: `audio.c` I2S and gain, `recorder_f32.c` / `recorder_q15.c` the DSP core, `input.c` joystick and encoder, `oled.c`, `imu.c`, `pins.h` per-target pins |
| `sdkconfig.defaults*` | shared, S3 and C3 build settings |
| `docs/DESIGN.md` | the design, the musical roadmap, the line-input note |
| `docs/IDEAS.md` | brainstormed features, none built |
| `docs/ENCLOSURE.md` | dimensions, sources, what still needs a caliper |
| `docs/pinmap.html` | the pin map page, also at https://claude.ai/artifact/Kuv8DJYi2QeyAciW6SBtmx |
| `main/usb/`, `main/usb.c` | USB composite: MIDI + UAC2 sound card, logs and reflash over SysEx |
| `main/ui.c`, `main/oled.c` | the screen: waveform, playhead, attitude ball, meters, menu; SSD1306 driver |
| `main/fx.c` | roll effects: delay (left) and stutter (right), S3 only |
| `main/slots.c`, `partitions.csv` | WAV sample slots on the FAT partition |
| `plugin/` | JUCE plugin, VST3 and standalone |
| `tools/` | `gr_midi.py` (log, cc, note, reboot), `flash.sh`, `uisim/` (renders the screens on the host) |
| `enclosure/` | OpenSCAD model, `render.sh`, previews in `out/` |
| `build/`, `build-c3/` | ESP-IDF output, not tracked |

## Two builds

| | ESP32-S3 SuperMini | ESP32-C3 SuperMini |
| - | - | - |
| Cores, FPU | 2 x 240 MHz, FPU | 1 x 160 MHz RISC-V, no FPU |
| Take | 20 s at 32 kHz, 16-bit, in PSRAM | as much as internal RAM allows at 16 kHz (about 5 to 7 s), 16-bit |
| DSP | float, core 1 | Q15 integer, high priority task on the one core |
| Screen | none | 0.42" 72x40 SSD1306 on I2C, 20 fps |
| Source | `recorder_f32.c` | `recorder_q15.c`, `oled.c` |

## Wiring

S3: same pins as the ESP32-S3 SuperMini kit, so a built kit runs this as is.
C3: fits the plain SuperMini and the "SuperMini with 0.42 OLED" board, whose screen is on 5/6.

| Signal | S3 GPIO | C3 GPIO | Goes to |
| - | -: | -: | - |
| BOOT | 0 | 9 | button on the board |
| BCLK | 7 | 4 | INMP441 SCK, PCM5102A BCK |
| WS | 8 | 3 | INMP441 WS, PCM5102A LCK |
| DOUT | 9 | 10 | PCM5102A DIN |
| DIN | 10 | 7 | INMP441 SD |
| SDA | 12 | 5 | MPU6050 SDA (C3: also the OLED) |
| SCL | 13 | 6 | MPU6050 SCL (C3: also the OLED) |
| ENC A | 4 | 0 | encoder CLK |
| ENC B | 5 | 1 | encoder DT |
| ENC SW | 6 | 20 | encoder push (C3: hold = menu, tap = next setting or tilt on/off) |

INMP441: VDD 3V3, L/R to GND (VDD also works, the firmware finds the slot).
PCM5102A: VIN 5V, SCK to GND, XSMT bridge to H, else it is silent.
MPU6050: VCC 3V3 (most breakouts have a regulator and accept 5V too), AD0 to GND.
Encoder: + to 3V3 (if the module has it), GND to GND. The pins use internal pull-ups.

## Build and flash

ESP-IDF v5.5 at `~/esp/esp-idf`:

```sh
. ~/esp/esp-idf/export.sh
cd ~/Projects/gesture-recorder

# S3
idf.py set-target esp32s3      # once
idf.py build
idf.py flash monitor           # board shows up on native USB as /dev/ttyACM0

# C3, in its own build directory so both stay built
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 set-target esp32c3   # once
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 build
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 flash monitor
```

On the C3 the log's first lines say how many seconds of take fit in RAM.

If the board does not enumerate, hold BOOT, tap RESET, release BOOT, then flash.

## What the serial log shows

```
I (612) rec: take buffer 20 s at 32000 Hz in PSRAM, 10 ms seam crossfade
I (620) imu: MPU6050 who_am_i 0x68, ready
I (630) audio: I2S full duplex, 32000 Hz, 64-frame blocks, core 1
I (1640) main: IDLE      rate +1.00 (target +0.05)  pos   0%  pitch  +1.2 roll  -0.4  cpu  3%  mic slot L
I (3210) main: RECORDING
I (5870) main: PLAYING  loop 2.65 s
```

## USB on the S3: MIDI, sound card, sample slots

Plug the S3 into a computer and it enumerates as **Gesture Recorder** with three faces:

- **A class compliant MIDI port.** Out: CC 1 pitch angle, CC 2 roll, CC 3 speed, CC 4 loop
  position, CC 5/6 joystick, CC 7 input level, note 60 while recording, note 62 while playing,
  and CC 20 to 30 echo every setting you change on the box. In: CC 20 to 30 set the settings
  (Mic gain, Mode, Pitch, Tilt, Speed max, Speed min, Snap, Grain, Volume, Input, Slot),
  note 60 held = record, note 61 = load the slot, note 63 = save the take to the slot.
- **A stereo sound card at 32 kHz, 16-bit.** Its capture is the box's output, so the loop lands
  on a DAW track. Its playback goes into the box: set Input to USB and hold record while the
  DAW plays, and the take is whatever the DAW sent. That is how DAW recordings get into the box.
- **Sample slots.** Eight WAV slots on a 2.4 MB flash partition, about 37 s in total at 32 kHz.
  Save and Load are menu items (and MIDI notes). Slots survive power off.

The S3's USB controller has four IN endpoints, which MIDI and audio use up, so there is no
serial console in this mode. Logs travel as MIDI SysEx instead:

```sh
python3 tools/gr_midi.py log          # the device log, plus the MIDI it sends
python3 tools/gr_midi.py cc 20 127    # mic gain to 36 dB
python3 tools/gr_midi.py note 60 on   # start recording (note 60 off stops)
tools/flash.sh                        # reflash: SysEx reboot into download mode, esptool, watchdog reset
```

If the board stays in download mode after a flash (no MIDI port, `lsusb` shows `303a:0009`),
unplug and replug it: esptool cannot always reset an S3 out of USB-OTG download mode.
Holding the encoder while plugging in skips USB mode and keeps the plain serial console.

## The plugin (VST3 and standalone)

`plugin/` is a JUCE plugin that mirrors the box: every setting is an automatable parameter
sent as MIDI, the top panel shows tilt, speed, loop position, level and the device log, and
buttons record, load, save and reboot-to-flash. It finds the box's MIDI port by itself. Audio
passes through untouched: put the device's USB capture on the plugin's track to hear the loop
in the DAW, and route the track back to the device's USB playback to sample from the DAW.

```sh
cd plugin
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/Projects/hikim/build/_deps/juce-src
cmake --build build -j8
# build/GestureRecorder_artefacts/Release/Standalone/"Gesture Recorder"
# build/GestureRecorder_artefacts/Release/VST3/"Gesture Recorder.vst3" (copied to ~/.vst3)
```

## Controls on the kit (S3)

BOOT sits inside the case, so the encoder push is the record button.

| Control | Play mode | Menu mode |
| - | - | - |
| Encoder push, hold | record | record |
| Encoder turn | change the selected setting | change the selected setting |
| Joystick long press (0.7 s) | enter menu mode | back to play mode |
| Joystick click | tilt on or off | next setting |
| Joystick up / down | speed when tilt is off | previous / next setting |
| Joystick left / right | pitch in GRAIN mode | adjust the setting |

Menu mode shows the setting and its value in an inverted block at the bottom of the screen.

Settings, in encoder order: Mic gain (0 to 36 dB, default 12), Mode (TAPE or GRAIN),
Pitch (semitones, GRAIN only), Tilt, Speed max (0.25 to 4x), Speed min (the crawl when
flat, 0 to 1x), Snap (speeds quantised to octaves and fifths), Grain (20 to 200 ms),
Volume. The top right of the screen is the input meter: a block at its right end means
the mic clipped, so turn Mic gain down.

TAPE is varispeed: speed and pitch are one thing. GRAIN keeps them separate, so you can
scrub backwards at half speed and still hear words at their own pitch, freeze the sound
at speed 0, or run at 1x and shift the pitch with the joystick.

## Check it on the board

1. Hold BOOT, speak, release. Flat on the desk it loops slowly forward.
2. Tilt nose down: faster. Nose up: backwards, faster with angle.
3. Wave it about: no zipper noise, no click at the loop seam.
4. No MPU6050 wired: forward at 1x, log says so, nothing crashes.
5. Hold BOOT for 30 s: recording stops writing at 20 s, playback still works.
6. The `cpu` figure in the status line stays in single digits at 2x.

If nose-down plays backwards, flip `TILT_SIGN` in `main/main.c`. If the sensor sits
sideways in the box, set `TILT_USE_ROLL` to 1.

## How it stays cheap

- One take as 16-bit samples in PSRAM, read through the cache in order.
- One fractional read head as integer plus fraction, so precision never shrinks
  deep into the take. Cubic Hermite interpolation, four loads a sample.
- Seam crossfade from a 320-entry table, only inside the first 10 ms of the loop.
- Rate slewed per sample with one multiply-add. No libm call in the audio path.
- `-O3 -ffast-math`, single precision throughout, audio task pinned to core 1
  in IRAM, 64-frame blocks with a three-descriptor DMA queue (about 6 ms).
- C3: the whole audio path is integer. Q16 read head and rate, Q15 seam weights,
  Hermite with three 64-bit products (mul + mulh on RV32IM), one-pole DC blocker.
  Nothing in the audio task ever logs, so the console can never stall it. The
  screen is written from the low priority UI task and the audio task preempts it.
