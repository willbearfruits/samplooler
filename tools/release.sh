#!/bin/sh
# Build the firmware and write to dist/:
#   samplooler-esp32s3.bin       merged image for offset 0x0 (bootloader + partition table + app) for a fresh board.
#                                Written over a board in use it keeps the sample slots but wipes the settings: the
#                                gap between the partition table and the app is the NVS, and a merged image fills it.
#   samplooler-esp32s3-boot.bin  bootloader + partition table alone, for 0x0 (ends before the NVS at 0x9000)
#   samplooler-esp32s3-app.bin   the app alone, for 0x10000
#                                boot + app, written as two parts, is the update that keeps both the slots and the
#                                settings: `esptool.py write_flash 0x0 ...-boot.bin 0x10000 ...-app.bin`. The page and the
#                                app write the parts the same way (tools/library.py site makes their manifest).
#   samplooler-esp32s3.elf       keep it with the release: it decodes the backtraces in crash reports
#   SHA256SUMS
set -e
cd "$(dirname "$0")/.."
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
mkdir -p dist
t=esp32s3
( cd firmware
  [ -f build/CMakeCache.txt ] || idf.py set-target $t
  idf.py build
  idf.py merge-bin -o samplooler-$t.bin
  python -m esptool --chip $t merge_bin -o build/samplooler-$t-boot.bin 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin )
cp firmware/build/samplooler-$t.bin dist/
cp firmware/build/samplooler-$t-boot.bin dist/
cp firmware/build/samplooler.bin dist/samplooler-$t-app.bin
cp firmware/build/samplooler.elf dist/samplooler-$t.elf
( cd dist && sha256sum samplooler-$t.bin samplooler-$t-boot.bin samplooler-$t-app.bin samplooler-$t.elf > SHA256SUMS && cat SHA256SUMS )
echo "version: $(git describe --tags --match 'v*' --always --dirty)"
