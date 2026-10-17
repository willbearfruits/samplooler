#!/bin/sh
# Build the firmware and write to dist/:
#   samplooler-esp32s3.bin      merged image for offset 0x0 (bootloader + partition table + app): the flasher's
#   samplooler-esp32s3-app.bin  the app alone, for 0x10000: an update that keeps the slots and settings
#   samplooler-esp32s3.elf      keep it with the release: it decodes the backtraces in crash reports
#   SHA256SUMS
# Also copies the merged image into site/firmware/ for a local preview of the browser flasher.
set -e
cd "$(dirname "$0")/.."
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
mkdir -p dist site/firmware
t=esp32s3
( cd firmware
  [ -f build/CMakeCache.txt ] || idf.py set-target $t
  idf.py build
  idf.py merge-bin -o samplooler-$t.bin )
cp firmware/build/samplooler-$t.bin dist/
cp firmware/build/samplooler.bin dist/samplooler-$t-app.bin
cp firmware/build/samplooler.elf dist/samplooler-$t.elf
cp dist/samplooler-$t.bin site/firmware/
( cd dist && sha256sum samplooler-$t.bin samplooler-$t-app.bin samplooler-$t.elf > SHA256SUMS && cat SHA256SUMS )
echo "version: $(git describe --tags --match 'v*' --always --dirty)"
