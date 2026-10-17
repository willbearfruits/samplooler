#!/bin/sh
# SING's vocoder, input stage and FX bus on the host (fx_test.c): every check, the cost, WAVs in out-fx/.
# The modules build with the firmware's warnings (errors) and the simulator's optimisation.
# VEC=0 ./run_fx.sh builds them without the host's vector units, closer to what the S3 can do.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
O=out-fx
mkdir -p $O
FLAGS="-std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Wno-format-truncation -I$M"
[ "${VEC:-1}" = 0 ] && FLAGS="$FLAGS -fno-tree-vectorize"
gcc $FLAGS -Wdouble-promotion -Werror -c $M/inst_vocoder.c -o $O/inst_vocoder.o
gcc $FLAGS -Wdouble-promotion -Werror -c $M/inst_fx.c -o $O/inst_fx.o
gcc -std=gnu17 -O2 -Wall -Wextra -Wno-format-truncation -I$M -c fx_test.c -o $O/fx_test.o
gcc -o $O/fx_test $O/fx_test.o $O/inst_vocoder.o $O/inst_fx.o -lm
cd $O && ./fx_test
