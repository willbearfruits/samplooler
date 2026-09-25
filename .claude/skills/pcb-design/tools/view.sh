#!/bin/bash
# view.sh <board-dir> <out.png> [WxH+X+Y]: copper on both layers without the pours, plus outlines,
# at ~20 px/mm; the optional crop is in mm in the board frame (origin top left).
set -e
T=$(cd "$(dirname "$0")" && pwd)
HW=$(git -C "$T" rev-parse --show-toplevel)/hardware
b=$1; out=$(realpath -m "$2"); crop=$3
W=$(mktemp -d)
cp "$HW/$b/"*.kicad_pro "$W/x.kicad_pro"
python3 "$T/nozone.py" "$HW/$b/"*.kicad_pcb "$W/x.kicad_pcb" 2>/dev/null || true
kicad-cli pcb export svg --layers B.Cu,F.Cu,Edge.Cuts,F.Fab,B.Fab --mode-single --page-size-mode 2 \
  --exclude-drawing-sheet -o "$W/x.svg" "$W/x.kicad_pcb" >/dev/null 2>&1
rsvg-convert -d 500 -p 500 -b white "$W/x.svg" -o "$out"
if [ -n "$crop" ]; then
  python3 - "$out" "$crop" <<'PY'
import sys, subprocess, re
f, c = sys.argv[1], sys.argv[2]
w, h, x, y = map(float, re.match(r'([\d.]+)x([\d.]+)\+([\d.]+)\+([\d.]+)', c).groups())
k = 500 / 25.4
subprocess.run(['magick', f, '-crop', f'{int(w*k)}x{int(h*k)}+{int(x*k)}+{int(y*k)}', '+repage', f], check=True)
PY
fi
rm -rf "$W"
echo "$out"
