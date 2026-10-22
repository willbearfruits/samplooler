#!/bin/sh
# Flash this firmware onto one board, found by name and serial, never onto a TWOFOLD.
#   A board running any of the family's firmwares (or the Samplooler): told over its own MIDI port to
#   reboot into download mode, then flashed on the serial port whose MAC matches its USB serial.
#   A board already in download mode (BOOT held while plugging it in): flashed if it is the only one.
#   No reset pulse before the write (esptool's default DTR/RTS toggle resets a board that is already in download mode,
#   and on a weak USB link that drops the port: two failed flashes on 2026-10-22); the MIDI reboot or BOOT put it there.
#   BOARD=<serial or MAC tail> picks one when there are several; PORT=/dev/... skips the search.
# Writes the bootloader, the partition table (the Samplooler's layout, so its sample slots survive) and
# the app; NVS and the slots are not touched.
set -e
cd "$(dirname "$0")/.."
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
[ -f firmware/build/flash_args ] || { echo "build first: cd firmware && idf.py build"; exit 1; }
PORT="${PORT:-$(python3 tools/flash_pick.py)}" || exit 1
[ -n "$PORT" ] || exit 1
echo "flashing $PORT"
cd firmware/build
esptool.py --chip esp32s3 -p "$PORT" -b 460800 --before no_reset --after watchdog_reset write_flash "@flash_args"
