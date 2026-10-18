#!/bin/sh
# tools/voicelab: VOICE alone (inst_voice.c with a few stand-ins), for listening and measuring fast.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
mkdir -p out
gcc -std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Wdouble-promotion -Werror -Wno-format-truncation -I. -I$M -I../sim -c $M/inst_voice.c -o out/inst_voice.o
gcc -std=gnu17 -O2 -Wall -Wno-format-truncation -I. -I$M -I../sim -c lab.c -o out/lab.o
gcc -o out/lab out/lab.o out/inst_voice.o -lm
