#!/bin/sh
# Render the screens on the host: compiles main/ui.c and main/oled.c against stub I2C,
# drives them through the states in sim.c, writes out/*.pbm + out/sheet.png (128x64,
# tinted like the two-colour panel).
set -e
cd "$(dirname "$0")"
P=../../firmware/main
mkdir -p out
gcc -std=gnu17 -O1 -I. -I$P -o sim sim.c stubs.c $P/ui.c $P/oled.c $P/font_data.c -lm
./sim
python3 render.py out/sheet.png out
