# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Samplooler (formerly "Gesture Recorder"; the local directory keeps the old name): a tilt-controlled
looper. Hold a button to record the mic, release and it loops; the IMU's pitch angle sets playback
speed and direction. One ESP-IDF firmware builds for two chips, plus a JUCE plugin, a browser
flasher site, an OpenSCAD enclosure, circuit boards and host-side tools. Public at
https://github.com/willbearfruits/samplooler (branch `main`).

`docs/BUILD.md` is the user-facing build guide; `README.md` is the short GitHub landing page that
points to it and to the project page (https://willbearfruits.github.io/samplooler/, deployed from
`site/` by CI). No board has been made and no case printed yet: keep the README's Status section and
all public text honest about that. `docs/DESIGN.md` is the design note; `docs/IDEAS.md` is unbuilt
ideas only; `docs/PRODUCTION.md` is the production-readiness review (what was hardened, what is open,
what the first hardware test should check).

## Commands

Firmware (ESP-IDF v5.5.1 at `~/esp/esp-idf`), run from `firmware/`. The S3 and C3 use separate build
dirs and sdkconfigs so both stay built:

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py set-target esp32s3                                        # S3, once -> build/
idf.py build flash monitor
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 set-target esp32c3   # C3, once -> build-c3/
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 build flash monitor
```

`sdkconfig` and `sdkconfig.c3` are generated and gitignored (`set-target` regenerates them), so config
changes belong in `sdkconfig.defaults`, `.esp32s3` or `.esp32c3`: CI and the published images are built
from those alone. The app builds with `-Werror`. The two build dirs share `managed_components/`: the C3
build (which needs no USB components) empties it, so the next S3 build fetches them again (network).
On the S3 the USB firmware replaces the serial console (see USB mode below): for `idf.py flash monitor`,
power it up with the encoder held; otherwise reflash with `tools/flash.sh` and read logs with
`sl_midi.py log`.

- `tools/release.sh [esp32s3|esp32c3]` — build both, write merged 0x0 images `samplooler-<target>.bin`
  to `dist/` (plus the app-only `-app.bin` for 0x10000 updates that keep the slots, the `.elf` for decoding
  crash reports, and SHA256SUMS) and copy the merged images into `site/firmware/`.
- `tools/flash.sh` — reflash an S3 that is running the USB firmware: sends SysEx `F0 7D 52 F7` to reboot
  into download mode, then esptool with `--after watchdog_reset`.
- `python3 tools/sl_midi.py log|status|reboot|cc N V|note N [on|off]` — talk to the S3 over raw ALSA MIDI
  (no pip deps). `log` is the only way to see S3 logs in USB mode; `status` asks for the status line and
  the last crash report (SysEx `F0 7D 53 F7`) and decodes the backtrace against `build/samplooler.elf`.
- `tools/uisim/run.sh` — compiles `firmware/main/ui.c` + `oled.c` + `font_data.c` on the host against
  stubs, renders every screen state for both targets to `tools/uisim/out/sheet.png` and `out/c3/sheet.png`
  (copied to `images/screens-s3.png` / `screens-c3.png` when they change). This is the way to check UI
  changes without hardware; there is no unit test suite.
- `python3 tools/fontconv.py` — regenerates `firmware/main/font_data.c` from the ASCII-art faces in
  `firmware/main/fonts/*.txt` (TWOFOLD's tiny/small/med/big). Run it after editing a glyph.
- `enclosure/render.sh` — OpenSCAD → `out/{shell,lid,plunger}.stl` + preview PNGs.
- `tools/showcase/make_showcase.sh` — the product video with sound (~25 min): `twin.c` compiles the
  firmware's `recorder_f32.c`, `fx.c`, `ui.c`, `oled.c` on the host and plays a scripted performance
  (`score.py`) into a soundtrack plus OLED frames; Blender tilts the stack in sync. `make_hardware_video.sh`
  is the boards-only cut. Both write to `hardware/images/`.
- Plugin: `cd plugin && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
  (VST3 is copied to `~/.vst3`). JUCE is fetched unless `-DFETCHCONTENT_SOURCE_DIR_JUCE=<checkout>` is
  given; a JUCE 8.0.13 checkout is at `~/Projects/hikim/build/_deps/juce-src`.

CI (`.github/workflows/build.yml`) builds both targets with ESP-IDF v5.5.1 (keep it on the same release
as the local toolchain) for pushes to `main`, pull requests and `v*` tags. On `main` the `pages` job
deploys `site/` with the fresh images in `site/firmware/` and the showcase video as
`site/media/showcase.mp4` (both gitignored); a `v*` tag attaches the images and SHA256SUMS to a
release. The flasher manifest's `version` (`site/firmware/manifest.json`) is set by hand; the app's own
version string is `git describe --match 'v*'` (the hardware `checkpoint/*` tags are ignored), so tag
releases `vX.Y.Z`.

## Firmware architecture (`firmware/main/`)

**One source tree, two targets, selected by `CONFIG_IDF_TARGET_ESP32C3`.** `pins.h` defines per-target
pins and capability flags (`HAS_USB`, `HAS_JOY`, `HAS_CONTROLS`, `PIN_REC`, `OLED_ENABLE`, `OLED_W/H`);
code is gated on those flags rather than on the target directly. Controls are one scheme on both chips:
a record button (`PIN_REC`, GPIO 3 on the S3; the C3 uses BOOT) and the encoder (tap = menu / dive /
back, hold = leave, turn = choose or adjust; `menu` and `menu_edit` in `main.c`). `HAS_JOY` is 0; set
it to 1 for a kit with the joystick (Y = speed when tilt is off, X = pitch in GRAIN). `recorder.h` picks the sample type:

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
Input, Slot, Save, Load — indexed `P_*`; the struct is `settings_t` in `settings.h`), tilt→speed mapping
(`TILT_SIGN`, `TILT_USE_ROLL`, dead zones), the slot jobs, and the MIDI map:
- CC 20+`p` carries setting `p` both ways, for the settings before Save/Load (`p < P_SAVE`).
- CC 1–7 go out on change: pitch, roll, speed, play position, joystick x, joystick y, input level.
- Notes in: 60 records while held, 61 loads, 63 saves. Notes out: 60 while recording, 62 while playing.

Adding a setting means updating the table, `param_adjust`, `param_text`, `setting_to_127` /
`setting_from_127`, the NVS field list in `settings.c` (a new 4-character tag; old blobs still load),
and the plugin (`enum Param` in `plugin/Source/PluginProcessor.h`, the parameter list in
`PluginProcessor.cpp`) to keep them in sync. Call `setting_changed()` after any change made outside
`param_adjust`; `settings.c` writes NVS 3 s later, never while recording.

**USB mode (S3).** At boot, unless the encoder is held, the Serial/JTAG console is replaced by a TinyUSB
composite device (MIDI + stereo UAC2 at `REC_SAMPLE_RATE`). All four IN endpoints are used, so there is
no CDC console: `esp_log` is redirected to SysEx `F0 7D 4C <text> F7`. SysEx in: `F0 7D 52 F7` ('R')
reboots into ROM download mode (after `app_prepare_reboot()`: settings flushed, output muted);
`F0 7D 53 F7` ('S') logs the status line and crash report. The UAC playback stream can be the record
source (Input = USB). The serial number is `SL-` + the factory MAC; every MIDI write goes through one
spinlock (logs come from any task).

**Slots** (`slots.c`, `partitions.csv`): 8 WAV files on a FAT/wear-levelling flash partition. Save and
load are chunked jobs (`slots_*_begin` / `slots_step`) driven by `job_poll()` in `main.c`: the output is
muted first (`audio_set_mute`, a 20 ms fade), the file moves in 16 KB steps so the screen and MIDI stay
alive, recording is ignored meanwhile. The take is reached through `recorder_take()` /
`recorder_load_begin/end()`, never while recording. A save goes to `slot.tmp` and is renamed at the end.

**Screen** (`ui.c`, `oled.c`, `font.h`). `main.c` fills a `ui_t` per frame (20 fps) and `ui_draw()` does
everything visual: a tape running between two reels carries the loop's envelope, a stick-figure operator
on top of it runs with the real speed and direction, tumbles above 4.5x, is echoed by the delay, stutters
with the stutter and juggles in GRAIN mode; slot jobs, boot notes (`CRASH`, `BROWNOUT`, `WATCHDOG`) and
the screensaver (3 min idle, drifting) each have a scene. `oled.c` has the primitives: proportional fonts
(`oled_ftext*`, faces `F_TINY/F_SMALL/F_MED/F_BIG`), `oled_pill` (filled pills draw black text),
rounded rects, dither, dotted lines. The old 5x7 `oled_text` stays for the showcase twin. Keep the
16-row header band: it is the yellow strip on two-colour panels.

**Robustness** (`diag.c`, `audio.c`, `oled.c`, `imu.c`). `diag_init()` is the first call in `app_main`;
`diag_crumb("step")` brackets risky steps (flash writes, USB start) and a `--wrap` of `esp_panic_handler`
keeps the panic text and backtrace in RTC memory, so the next boot shows `CRASH: step` / `BROWNOUT` /
`WATCHDOG` on the screen and in the log. The audio task counts DMA overruns, underruns and read errors
(`audio_get_stats`); call `audio_flash_hint()` before any flash write so stalls it causes are filed
separately. The speaker amp is enabled by the audio task after 50 ms of good clock. A screen or IMU
that stops answering is backed off and retried, not waited for. The task watchdog resets the device.

## Other parts

- `plugin/` — JUCE VST3/standalone that mirrors the device over MIDI: each device setting is an
  automatable parameter sent as CC 20+, telemetry CCs drive the display, SysEx log lines are shown.
  Audio passes through untouched. Keep it in sync with the CC/note map in `main.c`.
- `site/` — GitHub Pages browser flasher (esp-web-tools manifest pointing at `site/firmware/*.bin`) plus
  an in-browser demo (`demo.js`, `worklet.js`).
- `enclosure/samplooler.scad` — C3 case; unverified dimensions are single parameters at the top of the
  file, documented with sources in `docs/ENCLOSURE.md`. `docs/pinmap.html` mirrors `pins.h`.
- `hardware/` — PCBs for the S3 build, two designs: `stack-bottom/` + `stack-top/` (two 94 x 46 mm
  boards, 11 mm apart, joystick and encoder on the bottom one) and `mini/` (one board, no joystick,
  parts on both faces). Every KiCad file is generated by `hardware/gen/*.py`: change the Python and
  rerun `python3 hardware/gen/build.py [stack|mini]` (then `panel.py` for the one-piece JLC
  panels: the stack at 94 x 94 mm, and the Mini with the Samplooler Filter from
  `~/Projects/samplooler-filter` at 82 x 94 mm; then `render.py`); never hand-edit `stack-*/`,
  `panel-*/`, `mini/`, `lib/` or `fab/`. Designs live in `gen/stack.py` and `gen/mini.py`; shared parts in
  `gen/parts.py`, module geometry in `gen/modules.py` (its pin map must agree with
  `firmware/main/pins.h`), build steps in `gen/kit.py`. A build runs ERC, the mechanical model
  (`gen/mech.py`), Freerouting and DRC with schematic parity; all must end at 0 findings. The
  SuperMini's underside pads are reached by pogo pins through a fixed escape, and the breakouts take
  its lines in arrival order (`kit.supermini_exits`), so don't reorder breakout pins by hand. How to
  work on the boards (order of work, freezing a routed board, KiCad and Freerouting gotchas, debug
  tools) is the `pcb-design` skill in `.claude/skills/pcb-design/`; what the boards are is in
  `hardware/README.md`.
