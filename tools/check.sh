#!/bin/sh
# Everything that can be checked without a board, in one go. Each line is one of the host tests (the firmware's own
# C files, built for the PC); the last builds the firmware and says how much of the app partition it fills.
#   tools/check.sh            the host tests (about two minutes)
#   tools/check.sh --build    and the ESP-IDF build (needs ~/esp/esp-idf, a few minutes more)
#   tools/check.sh --long     the soak runs 8 simulated hours instead of 1
# Exit 0 only if every one passed. None of this tells what the board's own CPU, bus or sensors do: that is
# sl_midi.py status and the board check on the screen (SYSTEM > Board check).
cd "$(dirname "$0")/.."
BUILD=0; HOURS=1
for a in "$@"; do case "$a" in --build) BUILD=1 ;; --long) HOURS=8 ;; esac; done
fails=0
run() {   # run <name> <command...>: the last line it prints, and whether it passed
    name=$1; shift
    out=$("$@" 2>&1); code=$?
    last=$(printf '%s\n' "$out" | grep -v '^$' | tail -1)
    if [ $code -eq 0 ]; then printf '  ok    %-10s %s\n' "$name" "$last"; else printf '  FAIL  %-10s %s\n' "$name" "$last"; fails=$((fails + 1)); printf '%s\n' "$out" | grep -i 'fail\|error' | head -5; fi
}
echo "host tests"
run controls  tools/ctlsim/run.sh               # the take's start and end, the jog, the filter, the knob and the hand
run sensor    tools/tofsim/run.sh               # the hand's sensor against a pretend chip
run flock     tools/flocksim/run.sh             # the link: tempo, beats, key and scale
run voices    tools/voicesim/run.sh             # every voice on the input
run v1        rig/twin/build.sh                 # the motion and the old gestures, to the bit as they were
run screens   tools/uisim/run.sh                # every screen drawn (tools/uisim/out/sheet.png)
run screen-fuzz tools/uisim/run.sh fuzz         # the screen against random nonsense, sanitizers on
run soak      tools/soak/run.sh $HOURS          # a random performance through the whole audio path
if [ $BUILD -eq 1 ]; then
    echo "firmware"
    if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        out=$( (. "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1 && cd firmware && idf.py build) 2>&1 ); code=$?
        size=$(printf '%s\n' "$out" | grep 'samplooler.bin binary size' | tail -1)
        if [ $code -eq 0 ]; then printf '  ok    %-10s %s\n' build "$size"; else printf '  FAIL  %-10s\n' build; fails=$((fails + 1)); printf '%s\n' "$out" | grep -E 'error|FAILED' | head -10; fi
    else printf '  FAIL  %-10s no ESP-IDF at ~/esp/esp-idf\n' build; fails=$((fails + 1)); fi
fi
if [ $fails -eq 0 ]; then echo "all passed"; else echo "$fails failed"; fi
exit $fails
