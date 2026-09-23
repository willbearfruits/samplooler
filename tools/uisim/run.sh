#!/bin/sh
# Render the 128x64 screens on the host: compiles main/ui.c and main/oled.c against stub I2C,
# drives them through the states in sim.c, writes out/*.pbm and out/sheet.png.
set -e
cd "$(dirname "$0")"
P=../../main
mkdir -p out
gcc -std=gnu17 -O1 -I. -I$P -o sim sim.c stubs.c $P/ui.c $P/oled.c -lm
./sim
python3 render.py
