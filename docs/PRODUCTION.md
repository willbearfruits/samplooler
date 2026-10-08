# Production readiness

A review of the firmware, tooling and release process on 2026-10-08, with what was changed as a
result and what is still open. Nothing here has run on hardware yet: the changes build for both
chips and the screens render in the simulator, but the first board to run them will be the test.

The measures were weighed against TWOFOLD, a sibling USB audio interface on the same ESP32-S3
SuperMini wiring that went through a hardening pass before testers got it. What applied to a looper
with a microphone, a screen, a tilt sensor and sample slots on flash was ported; what did not
(OTA over WiFi with rollback, a web editor) was not.

## What was found

| Area | Finding | Status |
| - | - | - |
| Settings | Nothing persisted. Every power cycle went back to the defaults. | Fixed: `settings.c` |
| Slot save | A save wrote up to 1.3 MB from the UI loop in one go: the screen froze for ten seconds or more, MIDI stopped, and every flash erase stalled both cores while the loop kept playing, so the output stuttered. | Fixed: muted, chunked, with a progress screen |
| Slot save | A power cut mid-save left a half-written WAV in the slot. | Fixed: temp file and rename |
| Crashes | A panic in USB mode (no console) rebooted silently. Nothing said why. | Fixed: `diag.c` |
| Audio | `ESP_ERROR_CHECK` on the I2S read: a clock fault aborted the firmware. No count of dropouts anywhere. | Fixed: bounded reads, counters |
| Audio | The I2S channels started before the rest of the boot, so the RX queue overflowed before the audio task existed. | Fixed: started with the task |
| Speaker | The amp's enable pin went high before I2S existed: the DAC's wake-up reached the speaker. | Fixed: amp on after 50 ms of good clock |
| USB | Every device had the serial number `GR-0001`. Two on one host confuse ALSA and Windows. | Fixed: serial from the chip's MAC |
| USB | Log lines and MIDI from different tasks wrote the same TinyUSB FIFO with no lock. | Fixed: a spinlock around writes |
| USB | The reboot-to-download sequence was the bare two lines that TWOFOLD found unreliable. | Fixed: detach, SE0, hand the pads over, then reboot |
| I2C | A screen or sensor whose wire comes loose mid-run made the UI loop eat I2C timeouts: 100 ms per frame for the screen, 50 ms per read for the IMU. | Fixed: back-off and retry in `oled.c`, `imu.c` |
| Boot | A missing PSRAM halted the firmware with a blank screen. | Fixed: a fault screen |
| Watchdog | A hung main task only logged a warning, and in USB mode the warning went nowhere. | Fixed: task watchdog resets the device; `diag.c` reports WATCHDOG |
| Build | Warnings were allowed. | Fixed: `-Werror` on the app |
| Version | `git describe` picked the hardware checkpoint tags, so the app version read `checkpoint/boards-rev-b-1-g…`. | Fixed: only `v*` tags count |
| Release | No ELF kept with the images, so a tester's backtrace could not be decoded later. | Fixed: `tools/release.sh` keeps the ELF and the app-only image |
| Stacks | The main task had 3.5 KB for the UI, FATFS, NVS and printf with floats. | Raised to 6 KB (S3) and 5 KB (C3); the status report shows the high-water marks |

Looked at and left alone: brownout detection (on, at the default level), assertions (on), the
interrupt watchdog (on), flash mode (DIO, the safe choice for unknown SuperMini clones), the audio
path's no-allocation rule (holds), the C3 integer path (holds).

## What changed

**Settings persist** (`firmware/main/settings.c`). All eleven settings go to NVS under 4-character
tags, so a blob from an older or newer firmware still loads: unknown tags are skipped, missing ones
keep their defaults. Writes are debounced three seconds after the last change and never happen
while recording; while a loop plays they wait up to thirty seconds for an idle moment, then go
ahead (a page program is about a millisecond). The plugin sends all its parameters when it connects,
which the device treats as changes, so the device then saves the plugin's values.

**Slot jobs** (`firmware/main/slots.c`, `main.c`). Save and load run as chunked jobs from the
10 ms loop: the output fades out over 20 ms, the file moves in 16 KB steps while the screen shows
`SAVING 3` with a bar and MIDI keeps flowing, then the output fades back in. Recording is ignored
while a job runs. A save writes `slot.tmp` and renames it at the end. The result (`SAVED 3`,
`LOAD FAILED`, `SLOT 3 EMPTY`) shows for 1.5 s.

**Boot diagnostics** (`firmware/main/diag.c`). A crumb in RTC memory names the risky step in
progress (slots mount, slot save, settings save, USB start, the reboot-to-download steps). A
`--wrap` of `esp_panic_handler` records the panic reason or abort text and an 8-deep backtrace (pc
and ra only on the C3) in RTC memory before the real handler runs. At the next boot the reset reason
becomes a note (`CRASH: slot save`, `BROWNOUT`, `WATCHDOG`) shown on the screen for the first
seconds and in the log. SysEx `F0 7D 53 F7` logs the status line and the crash report;
`python3 tools/sl_midi.py status` prints them and decodes the backtrace against
`firmware/build/samplooler.elf` when the toolchain is on the path.

**Audio health** (`firmware/main/audio.c`). I2S reads and writes have a 100 ms timeout and count
failures instead of aborting. The DMA queue overflow interrupts count overruns and underruns;
within 500 ms of a flash write (`audio_flash_hint()`) they count as flash stalls instead. The CPU
figure now also keeps the worst block of the last second. A warning line goes out when the counters
move, and the status line once a minute.

**Mute ramp.** `audio_set_mute()` fades everything going out (DAC, speaker, USB) over 20 ms; slot
jobs and the reboot path use it.

**Speaker amp.** `PIN_AMP_SD` is held low with a pull-down from the first line of `app_main` and
raised by the audio task after 25 clean blocks.

**USB.** The serial number is `SL-` plus four bytes of the factory MAC. One spinlock covers every
writer of the MIDI IN endpoint. The reboot-to-download sequence detaches, drives SE0 for 100 ms,
hands the pads to the Serial/JTAG controller, waits for the host's bus reset, then sets the ROM's
download flag and restarts.

**Config.** `CONFIG_I2S_ISR_IRAM_SAFE=y`, `CONFIG_ESP_TASK_WDT_PANIC=y`, main task stack 6144 (S3)
and 5120 (C3). The app compiles with `-Werror`. The version is `git describe --match 'v*'`.

## What is still open

- **No hardware test of any of it.** The first power-on should go through the checklist in
  `docs/BUILD.md` section 7, then: save and load a slot while a loop plays (listen for the fade,
  watch the bar), change a setting and power-cycle after five seconds, send
  `tools/sl_midi.py status` and check that every audio counter is 0 after a minute of playing, then
  `tools/sl_midi.py reboot` and `tools/flash.sh` from the running USB firmware.
- **Updating keeps the slots only if the user says so.** The browser flasher asks whether to erase
  the chip; the partition table has not changed, so declining keeps the slots and settings. The
  app-only image in `dist/` can be written at `0x10000` with esptool for the same effect. There is
  no over-the-air path and no rollback: the device has no network and the flasher is the update.
- **USB PID.** `0x303A:0x4020` is Espressif's vendor ID with an unassigned product ID. Espressif
  hands out PIDs for open hardware through github.com/espressif/usb-pids; apply before boards go out.
- **The plugin's connect-time sync** overwrites the device's saved settings with the plugin's
  parameter values. Reading the device first would be the better direction.
- **No automated test of the DSP.** TWOFOLD compiles its DSP on the host and checks clicks at every
  parameter edge; the showcase twin (`tools/showcase/twin.c`) already compiles the recorder on the
  host, so a seam-click and speed-sweep test would be a small step from there.
- **Crash reports carry no version.** The status line does; a tester should send both.
- **Task watchdog on the main task only through the idle task.** A long I2C timeout storm would
  still be caught by the panic, but the main task itself is not registered with the watchdog.
- **Building the two targets in turn re-downloads the USB components.** The component manager
  keeps one `managed_components/` for both build directories, and the C3 build (which needs none)
  empties it, so the next S3 build needs the network. CI builds each target in its own checkout.
