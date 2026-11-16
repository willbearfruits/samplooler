#!/bin/sh
# The rev E controls on the host: the left knob's jog, the hand, the filter, and how a take starts and ends
# (recorder_f32.c, fx.c, gesture.c, tune.c as the board runs them), with the address and undefined-behaviour
# sanitizers on. Prints ok / FAIL lines and exits non-zero on a failure.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
gcc -std=gnu17 -O1 -g -ffast-math -fno-math-errno -Wall -Wextra -Werror -Wdouble-promotion -Wno-format-truncation \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. -I../host -I$M \
    -o ctlsim ctlsim.c $M/recorder_f32.c $M/fx.c $M/gesture.c $M/tune.c -lm
./ctlsim "$@"
