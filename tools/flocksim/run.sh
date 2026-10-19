#!/bin/sh
# The Samplooler on the link, on the host: flock.c, link.c and recorder_f32.c with pretend boards in a pretend air
# (flocksim.c). Checks what a take does to the flock's tempo, how a take snaps to a tempo already there and stays on
# its beat, and that the key and scale travel both ways.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
gcc -std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Werror -Wdouble-promotion -Wno-format-truncation -I. -I../host -I$M \
    -o flocksim flocksim.c $M/flock.c $M/link.c $M/recorder_f32.c -lm
./flocksim "$@"
