#!/bin/sh
# The VL53L1X driver on the host: firmware/main/tof.c, unchanged, against a fake sensor behind the I2C calls and a
# clock the test owns (tofsim.c). Checks the bring-up and the registers it leaves, the measurements and their rate,
# the bus time of every call, and what happens when the bus fails, the sensor is plugged in late, or it never boots.
# -v prints the driver's log lines. The binary is built outside the repo.
set -e
cd "$(dirname "$0")"
M=../../firmware/main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
gcc -std=gnu17 -O2 -ffast-math -Wall -Wextra -Werror -Wdouble-promotion -fsanitize=address,undefined -I. -I$M \
    -o "$OUT/tofsim" tofsim.c $M/tof.c
"$OUT/tofsim" "$@"
