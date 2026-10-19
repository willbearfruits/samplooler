#!/bin/sh
# A long run of the Samplooler's audio path on the host (soak.c): recorder_f32.c, fx.c, voice.c with SING's engines,
# flock.c and link.c with one pretend board, driven by a random performance. Hours of simulated time in minutes.
#   tools/soak/run.sh [hours]     default 2
set -e
cd "$(dirname "$0")"
M=../../firmware/main
gcc -std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Werror -Wdouble-promotion -Wno-format-truncation -I. -I../host -I$M \
    -o soak soak.c $M/recorder_f32.c $M/fx.c $M/voice.c $M/sing_voice.c $M/sing_vocoder.c $M/sing_fx.c $M/flock.c $M/link.c -lm
./soak "$@"
