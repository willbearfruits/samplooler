#!/bin/sh
# Reflash the S3 while it runs the USB MIDI/audio firmware.
#   1. SysEx F0 7D 52 F7 asks the app to reboot into the ROM download mode (USB-OTG CDC).
#   2. esptool writes the images.
#   3. esptool cannot pull an S3 out of USB-OTG download mode with RTS, so it resets it
#      through the RTC watchdog instead (--after watchdog_reset).
# If the app is not running (no MIDI port), skip step 1: the board is already in the ROM.
set -e
cd "$(dirname "$0")/.."
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
if python3 tools/gr_midi.py reboot 2>/dev/null; then sleep 3; fi
PORT="${PORT:-$(ls /dev/serial/by-id/*Espressif* /dev/ttyACM0 2>/dev/null | head -1)}"
cd build
esptool.py --chip esp32s3 -p "$PORT" -b 460800 --before default_reset --after watchdog_reset write_flash "@flash_args"
