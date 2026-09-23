#!/bin/sh
# Render the printable parts and a preview.
set -e
cd "$(dirname "$0")"
mkdir -p out
for p in shell lid plunger; do
  openscad -q -D "part=\"$p\"" -o out/$p.stl gesture_recorder.scad
  echo "out/$p.stl"
done
openscad -q -D 'part="assembly"' --autocenter --viewall --projection=p --imgsize=1400,1000 --camera=0,0,0,55,0,25,0 -o out/assembly.png gesture_recorder.scad
openscad -q -D 'part="plate"'    --autocenter --viewall --projection=o --imgsize=1400,900  --camera=0,0,0,0,0,0,0   -o out/plate_top.png gesture_recorder.scad
echo "out/assembly.png out/plate_top.png"
