#!/bin/sh
# The voice on the host: every voice through a synthetic singer (voicesim.c). WAVs in out/.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
mkdir -p out
gcc -std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Wdouble-promotion -Wno-format-truncation -I$M -o voicesim \
    voicesim.c $M/voice.c $M/sing_voice.c $M/sing_vocoder.c $M/sing_fx.c -lm
./voicesim
