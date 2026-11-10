#!/bin/sh
# The consoles on a bench (lab.c): builds the player, the song's text and every inst_con_*.c on the host, plays the
# bench tune through each console and checks it. ./run.sh [CONSOLE ...]; out/ gets the WAVs and results.json.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
FLAGS="-std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Wno-format-truncation -I$M"
mkdir -p obj out
rm -f obj/*.o lab
pids=""
for f in $M/inst_song.c $M/inst_player.c $(ls $M/inst_con_*.c); do gcc $FLAGS -Wdouble-promotion -Werror -c $f -o obj/$(basename $f .c).o & pids="$pids $!"; done
gcc $FLAGS -c lab.c -o obj/lab.o & pids="$pids $!"
for p in $pids; do wait $p; done
gcc -o lab obj/*.o -lm
./lab "$@"
