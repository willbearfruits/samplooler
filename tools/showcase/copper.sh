#!/bin/bash
# copper.sh <work dir>: each board's routing (no pours) in colour on a dark background, ~30 px/mm:
# back copper blue, front red (pads on both show pink), outline and holes grey.
set -e
V=$(cd "$(dirname "$0")" && pwd)
HW=$(git -C "$V" rev-parse --show-toplevel)/hardware
O=$1/copper; T=$O/tmp; mkdir -p "$T"
DPI=760
for b in stack-bottom stack-top mini; do
  cp "$HW/$b/samplooler-$b.kicad_pro" "$T/x.kicad_pro"
  python3 "$V/nozone.py" "$HW/$b/samplooler-$b.kicad_pcb" "$T/x.kicad_pcb" 2>/dev/null || true
  for L in F.Cu B.Cu Edge.Cuts; do
    kicad-cli pcb export svg --layers $L,Edge.Cuts --black-and-white --mode-single --page-size-mode 2 \
      --exclude-drawing-sheet -o "$T/$L.svg" "$T/x.kicad_pcb" >/dev/null 2>&1
    rsvg-convert -d $DPI -p $DPI -b white "$T/$L.svg" -o "$T/$L.png"
  done
  ( cd "$T"
    magick Edge.Cuts.png -negate -morphology Dilate Disk:1 edge.png
    magick F.Cu.png -negate f.png
    magick B.Cu.png -negate b.png
    W=$(magick identify -format %w f.png); H=$(magick identify -format %h f.png)
    magick -size ${W}x${H} xc:'#06080b' \
      \( -size ${W}x${H} xc:'#3f7fe0' b.png -alpha off -compose copy-opacity -composite \) -compose over -composite \
      \( -size ${W}x${H} xc:'#e8554a' f.png -alpha off -compose copy-opacity -composite \) -compose screen -composite \
      \( -size ${W}x${H} xc:'#8a939c' edge.png -alpha off -compose copy-opacity -composite \) -compose over -composite \
      "../$b.png" )
done
rm -rf "$T"
