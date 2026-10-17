#!/bin/sh
# SING's voice engine on the host against a synthetic singer (voice_test.c): every check, the cost per block,
# and WAVs to listen to in out-voice/ (results.txt has the numbers). Fails if any check fails.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
mkdir -p out-voice
gcc -std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Wdouble-promotion -Werror -Wno-format-truncation -DVOICE_TEST \
    -I$M -c $M/inst_voice.c -o out-voice/inst_voice.o
gcc -std=gnu17 -O2 -Wall -Wextra -Wno-format-truncation -DVOICE_TEST -I$M -c voice_test.c -o out-voice/voice_test.o
gcc -o out-voice/voice_test out-voice/inst_voice.o out-voice/voice_test.o -lm
./out-voice/voice_test
