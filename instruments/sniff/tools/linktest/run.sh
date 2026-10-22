#!/bin/sh
# The link (firmware/main/link.c) on the host: several pretend boards, drifting clocks, a lossy air.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
gcc -std=gnu17 -O2 -Wall -Wextra -Werror -Wdouble-promotion -Wno-format-truncation -I. -I$M -c $M/link.c -o link.o
gcc -std=gnu17 -O2 -Wall -Wextra -Wno-format-truncation -I$M -c linktest.c -o linktest.o
gcc -o linktest link.o linktest.o -lm
./linktest
