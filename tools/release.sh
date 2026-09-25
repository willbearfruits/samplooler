#!/bin/sh
# Build both targets and write single-file images to dist/, plus SHA256SUMS.
# Each image goes at offset 0x0 (bootloader + partition table + app merged).
#   tools/release.sh            # both
#   tools/release.sh esp32s3    # one
# Also copies them into site/firmware/ for a local preview of the browser flasher.
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
  cp dist/samplooler-$t.bin site/firmware/
done
( cd dist && sha256sum samplooler-*.bin > SHA256SUMS && cat SHA256SUMS )
