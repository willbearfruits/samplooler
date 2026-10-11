#!/bin/bash
# The Samplooler showcase video, sound included. The firmware's own DSP and screen code (recorder_f32.c,
# fx.c, ui.c, oled.c) play a scripted performance on this computer (twin.c, score.py); Blender animates
# the stack in sync with it, then the two PCB designs, the copper, the C3 case and the website.
#
#   tools/showcase/make_showcase.sh     -> hardware/images/samplooler-showcase-full.mp4 (~89 s, ~25 min)
#   WORK=<dir>    work directory (default: a new temp dir, kept for inspection)
#   STAGE=<n>     start at stage n, reusing WORK: 1 twin + sound, 2 boards (VRML, PCB shots, copper),
#                 3 the performance shot, 4 the case, 5 the website, 6 compose + encode
#
# Needs gcc, kicad-cli, Blender 5 (EEVEE on a GPU), OpenSCAD, chromium, ImageMagick 7, ffmpeg,
# Python 3 with numpy and Pillow, and the Roboto and JetBrains Mono fonts. The performance itself
# (what is played, the tilt curves, the captions) is score.py; the section layout is score.SECTIONS.
set -e
S=$(cd "$(dirname "$0")" && pwd)
R=$(git -C "$S" rev-parse --show-toplevel)
WORK=${WORK:-$(mktemp -d -t samplooler-showcase-XXXX)}
STAGE=${STAGE:-1}
P=$R/firmware/main
quiet() { grep -E "Error|Traceback" || true; }
echo "work dir: $WORK"
mkdir -p "$WORK"

if [ "$STAGE" -le 1 ]; then
  echo "1/6 the twin plays the score"
  gcc -std=gnu17 -O2 -ffast-math -I"$S/stubs" -I"$R/tools/uisim" -I"$P" -o "$WORK/twin" "$S/twin.c" \
      "$P/recorder_f32.c" "$P/fx.c" "$P/ui.c" "$P/oled.c" "$P/font_data.c" -lm
  python3 "$S/score.py" "$WORK/work"
  "$WORK/twin" "$WORK/work/score.txt" "$WORK/work/mic.f32" "$WORK/work/out.f32" "$WORK/work/oled.raw" "$WORK/work/ui.csv"
  python3 "$S/mix.py" "$WORK/work"
  python3 "$S/oled_frames.py" "$WORK/work" perf 4
fi
if [ "$STAGE" -le 2 ]; then
  echo "2/6 boards: VRML, the PCB shots, the copper"
  mkdir -p "$WORK/wrl"
  for b in stack-bottom stack-top mini; do
    kicad-cli pcb export vrml -f --no-dnp --units mm --user-origin 50x50mm \
      -o "$WORK/wrl/$b.wrl" "$R/hardware/$b/samplooler-$b.kicad_pcb" >/dev/null 2>&1
  done
  for shot in stack mini; do blender -b -P "$S/scene.py" -- "$WORK/wrl" $shot "$WORK/pcb" --eevee 2>&1 | quiet; done
  "$S/copper.sh" "$WORK"
fi
if [ "$STAGE" -le 3 ]; then
  echo "3/6 the performance shot"
  blender -b -P "$S/perf.py" -- "$WORK/work" "$WORK/wrl" "$WORK/frames-perf" 2>&1 | quiet
fi
if [ "$STAGE" -le 4 ]; then
  echo "4/6 the C3 case"
  mkdir -p "$WORK/case"
  for p in shell lid dac imu esp oled buttons mic plunger; do
    openscad -q -D 'part="none"' -D "sel=\"$p\"" -o "$WORK/case/$p.stl" "$S/parts.scad"
  done
  [ -f "$R/tools/uisim/out/c3/09_play_forward.pbm" ] || "$R/tools/uisim/run.sh" >/dev/null
  python3 "$S/c3screen.py" "$R/tools/uisim/out/c3/09_play_forward.pbm" "$WORK/case/c3screen.png"
  blender -b -P "$S/case.py" -- "$WORK/case" "$WORK/case/c3screen.png" "$WORK/frames-case" 2>&1 | quiet
fi
if [ "$STAGE" -le 5 ]; then
  echo "5/6 the website"
  mkdir -p "$WORK/site"
  chromium --headless=new --disable-gpu --hide-scrollbars --no-sandbox --user-data-dir="$WORK/site/profile" \
    --window-size=1440,7000 --virtual-time-budget=4000 --screenshot="$WORK/site/full.png" \
    "file://$R/site/index.html" >/dev/null 2>&1
fi
echo "6/6 compose and encode"
python3 "$S/compose.py" "$WORK/work" "$WORK/final"
ffmpeg -y -v error -framerate 24 -i "$WORK/final/%05d.png" -i "$WORK/work/soundtrack.wav" \
  -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -c:a aac -b:a 256k -ar 48000 -ac 2 \
  -shortest -movflags +faststart "$R/hardware/images/samplooler-showcase-full.mp4"
echo "-> $R/hardware/images/samplooler-showcase-full.mp4"
