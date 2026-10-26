#!/bin/sh
# The twin (twin.c) and the v1 check (test_v1.c), from the firmware's own sources. Output in rig/twin/build/.
#   rig/twin/build.sh          build both, run the v1 check
set -e
cd "$(dirname "$0")"
M=../../firmware/main
T=../../tools
mkdir -p build
CF="-std=gnu17 -O2 -ffast-math -fno-math-errno -Wall -Wextra -Wdouble-promotion -Wno-format-truncation -Wno-unused-parameter"
gcc $CF -Ihost -I$T/host -I$T/uisim -I$M -o build/twin twin.c \
    $M/tune.c $M/motion.c $M/gesture.c $M/rig.c $M/recorder_f32.c $M/fx.c $M/ui.c $M/oled.c $M/font_data.c -lm
gcc $CF -I$M -o build/test_v1 test_v1.c $M/tune.c $M/motion.c $M/gesture.c -lm
./build/test_v1
