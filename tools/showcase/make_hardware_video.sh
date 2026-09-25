#!/bin/bash
# make_hardware_video.sh: the boards-only video (no sound) -> hardware/images/samplooler-showcase.mp4
# (the full showcase with sound is make_showcase.sh)
#   QUICK=1 make_hardware_video.sh   renders one second per shot: a smoke test of the whole chain
#   WORK=<dir>              work directory (default: a temp dir, kept for inspection)
# Needs kicad-cli, Blender 5 (EEVEE, GPU), ImageMagick 7, ffmpeg, the Roboto and JetBrains Mono fonts.
set -e
V=$(cd "$(dirname "$0")" && pwd)
HW=$(git -C "$V" rev-parse --show-toplevel)/hardware
WORK=${WORK:-$(mktemp -d -t samplooler-video-XXXX)}
mkdir -p "$WORK/wrl"
echo "work dir: $WORK"
for b in stack-bottom stack-top mini; do
  kicad-cli pcb export vrml -f --no-dnp --units mm --user-origin 50x50mm \
    -o "$WORK/wrl/$b.wrl" "$HW/$b/samplooler-$b.kicad_pcb" >/dev/null 2>&1
done
RANGE=""; [ -n "$QUICK" ] && RANGE="0 23"
for shot in stack mini; do
  blender -b -P "$V/scene.py" -- "$WORK/wrl" $shot "$WORK/frames" $RANGE --eevee 2>&1 | grep -E "Error|Traceback" || true
done
"$V/cards.sh" "$WORK"
"$V/copper.sh" "$WORK"
"$V/assemble.sh" "$WORK"
if [ -z "$QUICK" ]; then
  cp "$WORK/samplooler-showcase.mp4" "$HW/images/samplooler-showcase.mp4"
  echo "-> $HW/images/samplooler-showcase.mp4"
fi
