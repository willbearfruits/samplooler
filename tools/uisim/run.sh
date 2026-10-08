#!/bin/sh
# Render the screens on the host: compiles main/ui.c and main/oled.c against stub I2C,
# drives them through the states in sim.c, writes out/*.pbm + out/sheet.png (S3, 128x64
# two-colour panel) and out/c3/*.pbm + out/c3/sheet.png (C3, 72x40).
set -e
cd "$(dirname "$0")"
P=../../firmware/main
mkdir -p out/c3
gcc -std=gnu17 -O1 -I. -I$P -o sim sim.c stubs.c $P/ui.c $P/oled.c $P/font_data.c -lm
gcc -std=gnu17 -O1 -DCONFIG_IDF_TARGET_ESP32C3=1 -I. -I$P -o sim-c3 sim.c stubs.c $P/ui.c $P/oled.c $P/font_data.c -lm
./sim
OUT=out/c3 ./sim-c3
python3 render.py out/sheet.png out
python3 render.py out/c3/sheet.png out/c3 mono
