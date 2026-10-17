#!/bin/sh
# The instrument on the host (see sim.h): builds the firmware's portable files with the stand-ins here,
# runs every check (./run.sh) or only the instrument's own (./run.sh inst), writes out/: screens
# (<name>.png), animations (<name>-*.gif), sound (<name>.wav and parts), results.json.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
# the instrument's own portable files (add any inst_*.c that builds on the host; device-only ones get a stand-in in test_inst.c)
INST_SRCS="$M/instrument.c $M/inst_engine.c"
CORE="front params looper calib ui oled font_data"
FLAGS="-std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Wno-format-truncation -I. -I$M"
mkdir -p obj out
rm -f obj/*.o sim
pids=""
for f in $CORE; do gcc $FLAGS -Wdouble-promotion -Werror -c $M/$f.c -o obj/$f.o & pids="$pids $!"; done
for f in $INST_SRCS; do gcc $FLAGS -Wdouble-promotion -Werror -c $f -o obj/inst_$(basename $f .c).o & pids="$pids $!"; done
for f in sim core_checks main test_inst; do gcc $FLAGS -c $f.c -o obj/$f.o & pids="$pids $!"; done
for p in $pids; do wait $p; done
gcc -o sim obj/*.o -lm
./sim "$@"
