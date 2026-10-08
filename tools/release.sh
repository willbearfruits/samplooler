#!/bin/sh
# Build both targets and write to dist/:
#   samplooler-<target>.bin      merged image for offset 0x0 (bootloader + partition table + app): the flasher's
#   samplooler-<target>-app.bin  the app alone, for 0x10000: an update that keeps the slots and settings
#   samplooler-<target>.elf      keep it with the release: it decodes the backtraces in crash reports
#   SHA256SUMS
#   tools/release.sh            # both
#   tools/release.sh esp32s3    # one
# Also copies the merged images into site/firmware/ for a local preview of the browser flasher.
set -e
cd "$(dirname "$0")/.."
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
mkdir -p dist site/firmware
for t in ${1:-esp32s3 esp32c3}; do
  case $t in
    esp32s3) b=build ;                  cfg=sdkconfig ;;
    esp32c3) b=build-c3 ;               cfg=sdkconfig.c3 ;;
    *) echo "unknown target $t"; exit 1 ;;
  esac
  ( cd firmware
    [ -f $b/CMakeCache.txt ] || idf.py -B $b -DSDKCONFIG=$cfg set-target $t
    idf.py -B $b -DSDKCONFIG=$cfg build
    idf.py -B $b -DSDKCONFIG=$cfg merge-bin -o samplooler-$t.bin )
  cp firmware/$b/samplooler-$t.bin dist/
  cp firmware/$b/samplooler.bin dist/samplooler-$t-app.bin
  cp firmware/$b/samplooler.elf dist/samplooler-$t.elf
  cp dist/samplooler-$t.bin site/firmware/
done
( cd dist && sha256sum samplooler-*.bin samplooler-*.elf > SHA256SUMS && cat SHA256SUMS )
echo "version: $(git describe --tags --match 'v*' --always --dirty)"
