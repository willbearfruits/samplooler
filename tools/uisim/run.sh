#!/bin/sh
# Render the screens on the host: compiles main/ui.c and main/oled.c against stub I2C,
# drives them through the states in sim.c, writes out/*.pbm + out/sheet.png (128x64,
# tinted like the two-colour panel).
#   run.sh fuzz [frames]   instead: ui_draw() against random nonsense, with the sanitizers on (fuzz.c)
set -e
cd "$(dirname "$0")"
P=../../firmware/main
if [ "$1" = fuzz ]; then
    gcc -std=gnu17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -I. -I$P \
        -o fuzz fuzz.c stubs.c $P/ui.c $P/oled.c $P/font_data.c -lm
    ./fuzz "${2:-300000}"
    exit
fi
mkdir -p out
rm -f out/*.pbm
gcc -std=gnu17 -O1 -I. -I$P -o sim sim.c stubs.c $P/ui.c $P/oled.c $P/font_data.c -lm
./sim
python3 render.py out/sheet.png out
