# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Samplooler (formerly "Gesture Recorder"; the repo directory keeps the old name): a tilt-controlled
looper. Hold a button to record the mic, release and it loops; the IMU's pitch angle sets playback
speed and direction. One ESP-IDF firmware builds for two chips, plus a JUCE plugin, a browser
flasher site, an OpenSCAD enclosure and host-side tools.

`docs/BUILD.md` is the current, user-facing build guide. `README.md` predates the rename and the move
into `firmware/` (it still says `main/`, `gr_midi.py`, "Gesture Recorder"), so trust BUILD.md and the
code over it. `docs/DESIGN.md` is the design note; `docs/IDEAS.md` is unbuilt ideas only.

## Commands

Firmware (ESP-IDF v5.5 at `~/esp/esp-idf`), run from `firmware/`. The S3 and C3 use separate build
dirs and sdkconfigs so both stay built:

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py set-target esp32s3 && idf.py build                                   # S3 -> build/
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 set-target esp32c3              # C3, once
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 build flash monitor
```

- `tools/release.sh [esp32s3|esp32c3]` — build both, write merged 0x0 images `samplooler-<target>.bin`
  to `dist/` (with SHA256SUMS) and copy them into `site/firmware/`.
- `tools/flash.sh` — reflash an S3 that is running the USB firmware: sends SysEx `F0 7D 52 F7` to reboot
  into download mode, then esptool with `--after watchdog_reset`.
- `python3 tools/sl_midi.py log|reboot|cc N V|note N [on|off]` — talk to the S3 over raw ALSA MIDI
  (no pip deps). `log` is the only way to see S3 logs in USB mode.
- `tools/uisim/run.sh` — compiles `firmware/main/ui.c` + `oled.c` on the host against stubs, renders
  every screen state for both targets to `tools/uisim/out/sheet.png` and `out/c3/sheet.png`. This is the
  way to check UI changes without hardware; there is no unit test suite.
- `enclosure/render.sh` — OpenSCAD → `out/{shell,lid,plunger}.stl` + preview PNGs.
- `tools/showcase/make_showcase.sh` — the product video with sound (~25 min): `twin.c` compiles the
  firmware's `recorder_f32.c`, `fx.c`, `ui.c`, `oled.c` on the host and plays a scripted performance
  (`score.py`) into a soundtrack plus OLED frames; Blender tilts the stack in sync. `make_hardware_video.sh`
  is the boards-only cut. Both write to `hardware/images/`.
- Plugin: `cd plugin && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=<juce checkout> && cmake --build build`
  (VST3 is copied to `~/.vst3`).

CI (`.github/workflows/build.yml`) builds both targets on every push, deploys `site/` + images to
GitHub Pages on main, and attaches images to a release on `v*` tags.

## Firmware architecture (`firmware/main/`)

**One source tree, two targets, selected by `CONFIG_IDF_TARGET_ESP32C3`.** `pins.h` defines per-target
pins and capability flags (`HAS_USB`, `HAS_JOY`, `HAS_CONTROLS`, `OLED_ENABLE`, `OLED_W/H`); code is
gated on those flags rather than on the target directly. `recorder.h` picks the sample type:

| | S3 | C3 |
| - | - | - |
| DSP | `recorder_f32.c`, float, 32 kHz, 20 s take in PSRAM, TAPE + GRAIN | `recorder_q15.c`, Q15/Q16 integer (no FPU), 16 kHz, RAM-sized take, TAPE only |
| Extras | `fx.c` roll effects, `usb.c` + `usb/` (TinyUSB MIDI + UAC2), 128x64 OLED, joystick | 72x40 OLED, encoder only |

Both recorder files are always in `SRCS`; each compiles to nothing on the other target. The USB
descriptor file and TinyUSB/UAC components are S3-only (see `firmware/main/CMakeLists.txt` and
`idf_component.yml`, including the `-u tud_descriptor_*` linker trick).

**Threading model — the core invariant.** `audio.c` runs a high-priority audio task (S3: pinned to
core 1, IRAM) doing I2S full-duplex in 64-frame blocks → `recorder_process()` → `fx_process()` (S3).
Everything else — IMU read, input, settings, UI drawing, MIDI, slots — runs in `app_main`'s 10 ms loop
in `main.c`. The UI side talks to the recorder only through `recorder_set_*()` plain 32-bit stores
that the audio task picks up per block. The audio path must not allocate, log, use `double`, or call
libm (built with `-O3 -ffast-math -Wdouble-promotion`); the C3 path must stay integer.

**`main.c`** owns the settings table (Mic gain, Mode, Pitch, Tilt, Speed max/min, Snap, Grain, Volume,
Input, Slot, Save, Load — indexed `P_*`), tilt→speed mapping (`TILT_SIGN`, `TILT_USE_ROLL`, dead zones),
and the MIDI mapping: CC 20+`p` mirrors setting `p` in both directions, CC 1–7 stream telemetry, notes
60/61/62/63 = record/load/playing/save. Adding a setting means updating the table, `param_adjust`,
`param_text`, the 0–127 conversions, and the plugin's parameter list to keep them in sync.

**USB mode (S3).** At boot, unless the encoder is held, the Serial/JTAG console is replaced by a TinyUSB
composite device (MIDI + stereo UAC2 at `REC_SAMPLE_RATE`). All four IN endpoints are used, so there is
no CDC console: `esp_log` is redirected to SysEx `F0 7D 4C <text> F7`. SysEx `F0 7D 52 F7` reboots into
ROM download mode. The UAC playback stream can be the record source (Input = USB).

**Slots** (`slots.c`, `partitions.csv`): 8 WAV files on a FAT/wear-levelling flash partition, accessed
via `recorder_take()` / `recorder_load_begin/end()` from the UI task, never while recording.

## Other parts

- `plugin/` — JUCE VST3/standalone that mirrors the device over MIDI: each device setting is an
  automatable parameter sent as CC 20+, telemetry CCs drive the display, SysEx log lines are shown.
  Audio passes through untouched. Keep it in sync with the CC/note map in `main.c`.
- `site/` — GitHub Pages browser flasher (esp-web-tools manifest pointing at `site/firmware/*.bin`) plus
  an in-browser demo (`demo.js`, `worklet.js`).
- `enclosure/samplooler.scad` — C3 case; unverified dimensions are single parameters at the top of the
  file, documented with sources in `docs/ENCLOSURE.md`. `docs/pinmap.html` mirrors `pins.h`.
- `hardware/` — PCBs for the S3 build, two designs: `stack-bottom/` + `stack-top/` (two boards, 11 mm
  apart, joystick and encoder on the bottom one) and `mini/` (one board, no joystick, parts on both
  faces). Every KiCad file is generated by `hardware/gen/*.py`: change the Python and rerun
  `python3 hardware/gen/build.py [stack|mini]` (then `render.py`); never hand-edit `stack-*/`, `mini/`,
  `lib/` or `fab/`. Designs live in `gen/stack.py` and `gen/mini.py`; shared parts in `gen/parts.py`,
  module geometry and tolerances in `gen/modules.py` (the pin map must agree with
  `firmware/main/pins.h`), build steps in `gen/kit.py`. The build runs ERC, the 3D mechanical model
  (`gen/mech.py`: parts on both faces, the gap between boards, trims, solder order), Freerouting, and
  DRC with schematic parity; all must stay at 0 findings. Nets come from eeschema's exported netlist.
  The SuperMini's 20 underside pads are reached with pogo pins through a fixed, locked escape
  (`kit.supermini_escape`); the breakouts take its lines in arrival order (`kit.supermini_exits`), so
  don't reorder breakout pins by hand. Routing tries `kit.ROUTE_VARIANTS` (Freerouting layer
  directions and via cost) and keeps the best; `prune_dangling` then drops escape stubs the router
  made redundant, and `rescue_gnd` ties walled-in GND pads and pour pockets to the planes on the real
  fill. Gotchas: the pcbnew Python `ZONE_FILLER` ignores edge and NPTH clearances, so the build lets
  `kicad-cli pcb drc --refill-zones --save-board` produce the fill that gets saved and plotted; pcbnew's
  connectivity on a board loaded from disk can disagree with kicad-cli DRC (use the DRC JSON as the
  judge); `ExportSpecctraDSN` segfaults on a board loaded from disk. See `hardware/README.md`.
